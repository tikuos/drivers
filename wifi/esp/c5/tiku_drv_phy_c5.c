/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_drv_phy_c5.c - C5 modem clocks and the pinned Espressif PHY lifecycle.
 * Register sequence and parameters: ESP-IDF 4d59230 C5 modem HAL and PHY data.
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_drv_phy_c5.h"
#include "phy_c5_abi.h"
#include <arch/esp32c5/tiku_analog_arch.h>
#include <arch/esp32c5/tiku_irq_arch.h>
#include <arch/esp32c5/tiku_systimer_arch.h>
#include <arch/esp32c5/tiku_esp32c5_regs.h>
#include <arch/esp32c5/tiku_cpu_common.h>
#include <kernel/drivers/tiku_drv.h>
#if (TIKU_THREADS_ENABLE + 0)
#include <kernel/threads/tiku_thread.h>
#endif

#define PHY_SYSCON     0x600A9C04u
#define PHY_SYSCON_MAP 0x600A9C0Cu
#define PHY_CLOCK      0x600A9C14u
#define PHY_LPCON      0x600AF018u
#define PHY_LPCON_MAP  0x600AF020u
#define PHY_WIFI_LPCLK 0x600AF00Cu
#define PHY_PMU_ACTIVE 0x600B000Cu
#define PHY_PMU_POWER  0x600B00CCu
#define PHY_PMU_SWITCH 0x600B00D0u
#define PHY_PMU_UPDATE 0x600B00DCu
#define PHY_CLOCKS     0x003BE5FFu
#define PHY_CAL_CLOCKS 0x000305FFu
#define PHY_SYS_BITS   ((1u << 12) | (1u << 28))
#define PHY_SYS_MAP    0x64646000u
#define PHY_LP_MAP     0x66660000u

/* C5 PHY defaults at a 10 dBm ceiling: bytes 1..39 cap rate groups.
 * Bytes 40..254 are zero; byte 255 is the C5 format terminator. */
static const c5_phy_init_t init_data = {
    {0,  40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,          40,
     40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,          40,
     40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, [255] = 0xF5}};

static const uint32_t clock_regs[] = {
    PHY_SYSCON,    PHY_CLOCK,      PHY_LPCON,     PHY_SYSCON_MAP,
    PHY_LPCON_MAP, PHY_PMU_ACTIVE, PHY_WIFI_LPCLK};
static const uint32_t clock_masks[] = {
    PHY_SYS_BITS, PHY_CLOCKS, 7u, PHY_SYS_MAP, PHY_LP_MAP, 3u << 30, 0xffffu};
static uint32_t saved[7];
static uint32_t saved_modem_mode;
static uint32_t calibration_us;
static unsigned calibrations;
static uint8_t active, changing, faulted;
static int calibration_result = -1;

/** @brief Change selected register bits, preserving other peripheral fields. */
static void field(uint32_t address, uint32_t mask, uint32_t bits)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    TIKU_C5_REG_WRITE(address,
                      (TIKU_C5_REG_READ(address) & ~mask) | (bits & mask));
    TIKU_C5_IRQ_RESTORE(state);
}

/** @brief Serialize foreground calls; reject workers, ISRs and masked-IRQ
 * callers. */
static int enter(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    int result = TIKU_C5_PHY_BUSY;
    if ((state & 8u) && !changing
#if (TIKU_THREADS_ENABLE + 0)
        && tiku_thread_in_kernel()
#endif
    ) {
        changing = 1;
        result = TIKU_C5_PHY_OK;
    }
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}

/** @brief Clear the foreground-call guard after publishing its result. */
static void leave(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    changing = 0;
    TIKU_C5_IRQ_RESTORE(state);
}

/** @brief Save digital clock fields and enable PHY/calibration clocks in PMU
 * active state. */
static void clocks_on(void)
{
    unsigned i;
    uint32_t state = TIKU_C5_IRQ_SAVE();
    saved_modem_mode = TIKU_C5_REG_READ(0x60098010u);
    /* Master 4 is the modem. Its DMA shares the native kernel's SRAM.
     * Other masters, access filters and write-lock bits are unchanged. */
    field(0x60098010u, 3u, 0);
    for (i = 0; i < 7; i++) {
        saved[i] = TIKU_C5_REG_READ(clock_regs[i]);
    }
    /* Wi-Fi power and coexistence clocks are required by the RF receive path.
     */
    field(PHY_WIFI_LPCLK, 0xffffu, 1u);
    field(PHY_LPCON, 7u, 7u);
    field(PHY_SYSCON, PHY_SYS_BITS, PHY_SYS_BITS);
    /* These write-one commands keep the analog buses powered. PLL power is
     * unchanged. */
    TIKU_C5_REG_WRITE(PHY_PMU_POWER, (1u << 28) | (1u << 29));
    field(PHY_PMU_ACTIVE, 3u << 30, 2u << 30);
    field(PHY_SYSCON_MAP, PHY_SYS_MAP, PHY_SYS_MAP);
    field(PHY_LPCON_MAP, PHY_LP_MAP, PHY_LP_MAP);
    TIKU_C5_REG_WRITE(PHY_PMU_UPDATE, 1u << 31);
    TIKU_C5_REG_WRITE(PHY_PMU_SWITCH, 1u << 28);
    field(PHY_CLOCK, PHY_CLOCKS, PHY_CLOCKS);
    TIKU_C5_IRQ_RESTORE(state);
}

/** @brief Restore the digital clock fields changed by clocks_on. */
static void clocks_off(void)
{
    unsigned i;
    uint32_t state = TIKU_C5_IRQ_SAVE();
    for (i = 0; i < 7; i++) {
        field(clock_regs[i], clock_masks[i], saved[i]);
    }
    field(0x60098010u, 3u, saved_modem_mode);
    TIKU_C5_REG_WRITE(PHY_PMU_UPDATE, 1u << 31);
    TIKU_C5_REG_WRITE(PHY_PMU_SWITCH, 1u << 28);
    TIKU_C5_IRQ_RESTORE(state);
}

/** @brief Run full calibration on the foreground stack after acquiring the
 * analog hardware. */
__attribute__((noinline)) static int calibrate(uint64_t start)
{
    c5_phy_calibration_t cal = {0};
    uint64_t end;
    (void)tiku_cpu_c5_unique_id(cal.mac, sizeof(cal.mac));
    calibration_result = register_chipv7_phy(&init_data, &cal, 2);
    /* 1 (ESP_CAL_DATA_CHECK_FAIL) reports the supplied data as stale; the full
     * calibration still ran, and ESP-IDF's phy_init.c proceeds on it. */
    if ((calibration_result != 0 && calibration_result != 1) ||
        tiku_c5_systimer_read(&end) != 0) {
        faulted = 1;
        return TIKU_C5_PHY_FAULT;
    }
    calibration_us = (uint32_t)(((end - start) & ((1ULL << 52) - 1)) / 16u);
    calibrations++;
    return TIKU_C5_PHY_OK;
}

int tiku_drv_phy_c5_on(void)
{
    uint64_t start;
    unsigned xtal;
    int result = enter();
    if (result) {
        return result;
    }
    if (faulted) {
        result = TIKU_C5_PHY_FAULT;
        goto done;
    }
    if (active) {
        goto done;
    }
    if ((TIKU_C5_REG_READ(0x60098010u) & 7u) > 4u) {
        result = TIKU_C5_PHY_BUSY;
        goto done;
    }
    /* The PLL feeds the modem; the kernel runs the CPU from it (PCR
     * SYSCLK_CONF source 3) whenever it is up. */
    xtal = (TIKU_C5_REG_READ(0x60096110u) >> 24) & 127u;
    if ((xtal != 40u && xtal != 48u) ||
        ((TIKU_C5_REG_READ(0x60096110u) >> 16) & 3u) != 3u ||
        tiku_c5_systimer_read(&start) != 0) {
        result = TIKU_C5_PHY_CLOCK;
        goto done;
    }
    if (tiku_c5_analog_acquire(TIKU_C5_ANALOG_PHY) != 0) {
        result = TIKU_C5_PHY_BUSY;
        goto done;
    }
    clocks_on();
    if ((TIKU_C5_REG_READ(PHY_CLOCK) & PHY_CLOCKS) != PHY_CLOCKS ||
        (TIKU_C5_REG_READ(PHY_LPCON) & 7u) != 7u ||
        (TIKU_C5_REG_READ(0x60098010u) & 3u) != 0) {
        clocks_off();
        (void)tiku_c5_analog_release(TIKU_C5_ANALOG_PHY);
        result = TIKU_C5_PHY_CLOCK;
        goto done;
    }
    if (!calibrations) {
        result = calibrate(start);
        if (result != TIKU_C5_PHY_OK) {
            goto done;
        }
    } else {
        phy_wakeup_init();
    }
    field(PHY_CLOCK, PHY_CAL_CLOCKS, saved[1]);
    field(PHY_SYSCON, 1u << 28, saved[0]);
    active = 1;
done:
    leave();
    return result;
}

int tiku_drv_phy_c5_off(void)
{
    int result = enter();
    if (result) {
        return result;
    }
    if (faulted) {
        result = TIKU_C5_PHY_FAULT;
        goto done;
    }
    if (active) {
        phy_close_rf();
        phy_xpd_tsens();
        phy_wait_freq_hw_hop_done();
        active = 0;
        clocks_off();
        (void)tiku_c5_analog_release(TIKU_C5_ANALOG_PHY);
    }
done:
    leave();
    return result;
}

int tiku_drv_phy_c5_active(void)
{
    return active;
}
unsigned tiku_drv_phy_c5_calibrations(void)
{
    return calibrations;
}
uint32_t tiku_drv_phy_c5_calibration_us(void)
{
    return calibration_us;
}
int tiku_drv_phy_c5_result(void)
{
    return calibration_result;
}

/** @brief Register the PHY without enabling RF activity at boot. */
static int driver_init(void)
{
    return TIKU_DRV_OK;
}

const tiku_drv_t tiku_drv_phy_c5 = {.name = "phy-c5",
                                    .class = TIKU_DRV_CLASS_RADIO,
                                    .init = driver_init,
                                    .deinit = tiku_drv_phy_c5_off};
