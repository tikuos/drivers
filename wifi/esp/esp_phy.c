/*
 * Tiku Drivers - ESP32-C61 radio: the modem's clocks, the PHY, the MAC
 *
 * The modem's clocks come in groups the PHY and the MAC each need; the PHY
 * is calibrated the first time it comes up and woken from its kept state
 * after.  Registers are the C61's MODEM_SYSCON and MODEM_LPCON blocks;
 * the sequence follows what IDF does, read as reference, never copied.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tiku.h"
#include <arch/esp32c61/tiku_cpu_common.h>
#include <arch/esp32c61/tiku_irq_arch.h>
#include <arch/esp32c61/tiku_esp32c61_regs.h>

#include "esp_heap.h"
#include "esp_port.h"

/* MODEM_SYSCON: the modem's digital clocks, resets and clock-gate maps. */
#define SYSCON_BASE         0x600A9C00UL
#define SYSCON_CLK_CONF     (SYSCON_BASE + 0x04UL)
#define SYSCON_ICG_MAPS     (SYSCON_BASE + 0x0CUL)
#define SYSCON_RST_CONF     (SYSCON_BASE + 0x10UL)
#define SYSCON_CLK_CONF1    (SYSCON_BASE + 0x14UL)

#define CONF_I2C_MST_160M   (1UL << 12)         /* in SYSCON_CLK_CONF */
#define CONF_SEC_APB        (1UL << 28)
#define RST_WIFIMAC         (1UL << 9)

/* SYSCON_CLK_CONF1: the Wi-Fi baseband's clocks, its MAC, the front end. */
#define CLK_WIFIBB          0x1FBUL             /* 22M..160X1 but 44M */
#define CLK_WIFIBB_44M      (1UL << 2)
#define CLK_WIFIMAC         (1UL << 9)
#define CLK_WIFI_APB        (1UL << 10)
#define CLK_FE_80M          (1UL << 13)
#define CLK_FE_160M         (1UL << 14)
#define CLK_FE_APB          (1UL << 15)
#define CLK_BT_APB          (1UL << 16)
#define CLK_BTBB            (1UL << 17)
#define CLK_FE_PWDET        (1UL << 19)
#define CLK_FE_ADC          (1UL << 20)
#define CLK_FE_DAC          (1UL << 21)

/* What the PHY's own init checks for before it calibrates. */
#define CLK_PHY_REQUIRED    0x3BE5FFUL

/* MODEM_LPCON: the modem's low-power clocks and their gate maps. */
#define LPCON_BASE          0x600AF000UL
#define LPCON_WIFI_LP_CLK   (LPCON_BASE + 0x0CUL)
#define LPCON_CLK_CONF      (LPCON_BASE + 0x18UL)
#define LPCON_ICG_MAPS      (LPCON_BASE + 0x20UL)

#define LP_SEL_MSK          0xFFFFUL            /* source bits, divider */
#define LP_SEL_RC_SLOW      (1UL << 0)
#define LPCON_WIFIPWR       (1UL << 0)
#define LPCON_COEX          (1UL << 1)
#define LPCON_I2C_MST       (1UL << 2)

/* Clock-gate maps: a domain runs in the PMU modem states its bits name;
 * 2 = the modem state, 4 = active (IDF's ICG codes 1 and 2). */
#define ICG_ACTIVE          4UL
#define ICG_ACTIVE_MODEM    6UL
#define SYSCON_ICG_OR       ((ICG_ACTIVE_MODEM << 12) | (ICG_ACTIVE << 16) | \
                             (ICG_ACTIVE_MODEM << 20) | (ICG_ACTIVE << 24) | \
                             (ICG_ACTIVE_MODEM << 28))
#define LPCON_ICG_OR        ((ICG_ACTIVE_MODEM << 16) | (ICG_ACTIVE_MODEM << 20) | \
                             (ICG_ACTIVE_MODEM << 24) | (ICG_ACTIVE_MODEM << 28))

/* The groups: the PHY's front end, its calibration, and the Wi-Fi MAC. */
#define MOD_PHY             (1U << 0)
#define MOD_PHY_CAL         (1U << 1)
#define MOD_WIFI            (1U << 2)

/* The PHY's defaults: version 1, then TX power caps in quarter dBm per rate
 * group (at most 20 dBm), the rest zero, and a closing 0x51. */
static const uint8_t phy_init_data[128] = {
    0x01, 0x00,
    0x50, 0x50, 0x50, 0x50, 0x50, 0x4C, 0x50, 0x50, 0x4C,
    0x48, 0x44, 0x3C, 0x3C, 0x3C, 0x4C, 0x4C, 0x4C, 0x48,
    [127] = 0x51
};

static uint8_t  modem_mods;
static uint8_t  wifi_inited;
static uint8_t  phy_on;
static uint8_t  phy_calibrated;
static int      phy_cal_rc = -1;
static uint32_t phy_cal_us;
static uint8_t  phy_cal_fresh;
static uint32_t phy_track_timer[5];     /* the libraries' timer shape */
static void    *phy_lock;               /* a pool lock: kept, like theirs */

/** @brief CLK_CONF1 bits a group needs. */
static uint32_t mod_clk1(uint8_t mods) {
    uint32_t c = 0UL;

    if (mods & MOD_PHY) {
        c |= CLK_FE_80M | CLK_FE_APB | CLK_FE_160M | CLK_FE_ADC |
             CLK_FE_DAC | CLK_FE_PWDET;
    }
    if (mods & MOD_PHY_CAL) {
        c |= CLK_WIFI_APB | CLK_WIFIBB | CLK_WIFIBB_44M | CLK_BTBB |
             CLK_BT_APB;
    }
    if (mods & MOD_WIFI) {
        c |= CLK_WIFIMAC | CLK_WIFI_APB | CLK_WIFIBB | CLK_WIFIBB_44M;
    }
    return c;
}

/**
 * @brief Move the modem's clocks from one set of groups to another.  The
 *        front end's clocks, once on, stay on; so do the Wi-Fi ones once
 *        the stack is up; and the analog I2C master never stops, as the
 *        clock tree's own PLL writes go through it too.
 */
static void modem_set(uint8_t mods) {
    uint32_t keep = CLK_FE_80M | CLK_FE_APB | CLK_FE_160M | CLK_FE_ADC |
                    CLK_FE_DAC | CLK_FE_PWDET;
    uint32_t want = mod_clk1(mods), m;

    if (wifi_inited) {
        keep |= mod_clk1(MOD_WIFI);
    }
    m = tiku_esp32c61_mie_off();
    TIKU_REG32(SYSCON_CLK_CONF1) =
        (TIKU_REG32(SYSCON_CLK_CONF1) & (keep | ~mod_clk1(modem_mods))) | want;
    if (mods & MOD_PHY_CAL) {
        TIKU_REG32(SYSCON_CLK_CONF) |= CONF_SEC_APB;
    } else if (modem_mods & MOD_PHY_CAL) {
        TIKU_REG32(SYSCON_CLK_CONF) &= ~CONF_SEC_APB;
    }
    if (mods & MOD_WIFI) {
        TIKU_REG32(LPCON_CLK_CONF) |= LPCON_COEX;
    } else if ((modem_mods & MOD_WIFI) && !wifi_inited) {
        TIKU_REG32(LPCON_CLK_CONF) &= ~LPCON_COEX;
    }
    modem_mods = mods;
    tiku_esp32c61_mie_restore(m);
}

void espw_modem_init(void) {
    uint32_t m = tiku_esp32c61_mie_off();

    /* What IDF's second-stage bootloader and clock init leave set, and this
     * kernel's boot, straight from the ROM, does not: the analog I2C
     * master clocked from 160 MHz, the RF's I2C powered, and the modem's
     * clock domains ungated while the PMU is active (code 2). */
    TIKU_REG32(LPCON_CLK_CONF) |= LPCON_I2C_MST;
    TIKU_REG32(SYSCON_CLK_CONF) |= CONF_I2C_MST_160M;
    TIKU_REG32(ESP32C61_PMU_IMM_HP_CK_POWER) =
        ESP32C61_PMU_TIE_HIGH_BB_I2C | ESP32C61_PMU_TIE_HIGH_PLL_I2C;
    TIKU_REG32(ESP32C61_PMU_HP_ACT_ICG_MODEM) = ESP32C61_PMU_MODEM_CODE_ACTIVE;
    TIKU_REG32(SYSCON_ICG_MAPS) |= SYSCON_ICG_OR;
    TIKU_REG32(LPCON_ICG_MAPS) |= LPCON_ICG_OR;
    TIKU_REG32(ESP32C61_PMU_IMM_MODEM_ICG) = ESP32C61_PMU_UPDATE_ICG_MODEM;
    TIKU_REG32(ESP32C61_PMU_IMM_SLEEP_SYSCLK) = ESP32C61_PMU_UPDATE_ICG_SWITCH;

    /* The MAC's sleep clock follows the system's slow clock: RC_SLOW. */
    TIKU_REG32(LPCON_WIFI_LP_CLK) =
        (TIKU_REG32(LPCON_WIFI_LP_CLK) & ~LP_SEL_MSK) | LP_SEL_RC_SLOW;
    TIKU_REG32(LPCON_CLK_CONF) |= LPCON_WIFIPWR;
    modem_mods = 0U;
    wifi_inited = 0U;
    tiku_esp32c61_mie_restore(m);
    if (phy_lock == NULL) {
        phy_lock = espw_osi_funcs._recursive_mutex_create();
    }
}

void espw_modem_wifi_clock_on(void) {
    modem_set(modem_mods | MOD_WIFI);
}

void espw_modem_wifi_clock_off(void) {
    modem_set(modem_mods & (uint8_t)~MOD_WIFI);
}

void espw_modem_wifi_reset(void) {
    uint32_t m = tiku_esp32c61_mie_off();

    TIKU_REG32(SYSCON_RST_CONF) |= RST_WIFIMAC;
    TIKU_REG32(SYSCON_RST_CONF) &= ~RST_WIFIMAC;
    tiku_esp32c61_mie_restore(m);
}

/*---------------------------------------------------------------------------*/
/* The PHY                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Every second while on: the PLL follows the die's temperature. */
static void phy_track(void *arg) {
    (void)arg;
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    if (phy_on) {
        phy_param_track_tot(true, false);
    }
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}

/** @brief First enable: the full RF calibration, kept by the PHY after. */
static void phy_calibrate(void) {
    esp_phy_calibration_data_t *cal = espw_calloc(1U, sizeof *cal);
    int64_t t0;

    if (cal == NULL) {
        ESPW_PRINTF("phy: no memory for calibration data\n");
        phy_cal_rc = -1;
        return;
    }
    phy_init_param_set(1U);             /* Wi-Fi and BLE share this PHY */
    t0 = espw_time_us();
    phy_cal_rc = register_chipv7_phy((const esp_phy_init_data_t *)
                                     (const void *)phy_init_data,
                                     cal, PHY_RF_CAL_FULL);
    phy_cal_us = (uint32_t)(espw_time_us() - t0);
    espw_free(cal);
    phy_calibrated = 1U;
    phy_cal_fresh = 1U;
}

void espw_phy_enable(void) {
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    if (!phy_on) {
        modem_set(modem_mods | MOD_PHY | MOD_PHY_CAL);
        if ((TIKU_REG32(SYSCON_CLK_CONF1) & CLK_PHY_REQUIRED) !=
            CLK_PHY_REQUIRED) {
            ESPW_PRINTF("phy: modem clocks 0x%08lx, want 0x%08lx\n",
                        (unsigned long)TIKU_REG32(SYSCON_CLK_CONF1),
                        (unsigned long)CLK_PHY_REQUIRED);
        }
        if (!phy_calibrated) {
            phy_calibrate();
        } else {
            phy_wakeup_init();
        }
        espw_timer_setfn(phy_track_timer, (void *)phy_track, NULL);
        espw_timer_arm_us(phy_track_timer, 1000000U, true);
        modem_set(modem_mods & (uint8_t)~MOD_PHY_CAL);
        phy_on = 1U;
        phy_param_track_tot(true, false);
    }
    phy_wifi_enable_set(1U);
    /* IDF's setting: the baseband's idle check off, as RX can panic. */
    set_bb_wdg(true, false, 0x18U, 0xAAU, false, false, false);
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}

void espw_phy_disable(void) {
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    phy_wifi_enable_set(0U);
    if (phy_on) {
        phy_on = 0U;
        espw_timer_disarm(phy_track_timer);
        phy_close_rf();
        phy_xpd_tsens();
        phy_wait_freq_hw_hop_done();
        modem_set(modem_mods & (uint8_t)~MOD_PHY);
    }
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}

int espw_phy_cal_result(uint32_t *us, int *fresh) {
    if (us != NULL) {
        *us = phy_cal_us;
    }
    if (fresh != NULL) {
        *fresh = phy_cal_fresh;
    }
    phy_cal_fresh = 0U;
    return phy_cal_rc;
}

/** @brief The Wi-Fi clocks stay on from here: the stack is up. */
void espw_modem_wifi_inited(int on) {
    wifi_inited = (uint8_t)(on != 0);
}

/*---------------------------------------------------------------------------*/
/* The MAC address                                                           */
/*---------------------------------------------------------------------------*/

int espw_read_mac(uint8_t *mac, unsigned int type) {
    if (mac == NULL) {
        return ESP_FAIL;
    }
    (void)tiku_cpu_esp32c61_unique_id(mac, 6U);
    /* The station has the factory address; the soft-AP its locally
     * administered twin. */
    if (type == ESP_MAC_WIFI_SOFTAP) {
        mac[0] |= 0x02U;
    }
    return ESP_OK;
}
