/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_c5_phy.c - C5 Wi-Fi clock groups over a foreground-owned PHY lifecycle.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_c5_arch.h"
#include "tiku_drv_phy_c5.h"
#include "../esp_port.h"
#if (TIKU_DRV_BLE_ESP_ENABLE + 0)
#include "../esp_ble_abi.h"
#endif
#include <arch/esp32c5/tiku_cpu_common.h>
#include <arch/esp32c5/tiku_timer_arch.h>
#include <arch/esp32c5/tiku_esp32c5_regs.h>

static uint32_t saved_lp, saved_lp_clock, saved_lp_map, saved_rng;
static uint32_t track_timer[5];
static uint8_t prepared, inited, phy_used;
#if (TIKU_DRV_BLE_ESP_ENABLE + 0)
static uint8_t btbb_users;              /* the baseband's users, both MACs */
#endif
static unsigned reported_calibrations;
static void *phy_lock;

/** @brief Update selected modem fields without modifying other owners' bits. */
static void field(uint32_t a, uint32_t mask, uint32_t bits)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    TIKU_C5_REG_WRITE(a, (TIKU_C5_REG_READ(a) & ~mask) | (bits & mask));
    TIKU_C5_IRQ_RESTORE(state);
}
/** @brief Track the active Wi-Fi PHY from the vendor timer worker. */
static void track(void *argument)
{
    (void)argument;
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    if (prepared && phy_used) {
        phy_param_track_tot((phy_used & 1u) != 0, (phy_used & 6u) != 0);
    }
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
int espw_c5_radio_prepare(void)
{
    if (prepared) {
        return 0;
    }
    if (tiku_drv_phy_c5_active()) {
        return -1;
    }
    if (tiku_drv_phy_c5_on() != 0) {
        return -1;
    }
    saved_lp = TIKU_C5_REG_READ(0x600AF018u);
    saved_lp_clock = TIKU_C5_REG_READ(0x600AF00Cu);
    saved_lp_map = TIKU_C5_REG_READ(0x600AF020u);
    saved_rng = TIKU_C5_REG_READ(0x600B2800u);
    field(0x600B2800u, 1u << 24, 1u << 24);
    prepared = 1;
    return 0;
}
void espw_c5_radio_release(void)
{
    if (!prepared) {
        return;
    }
    phy_used = 0;
#if (TIKU_DRV_BLE_ESP_ENABLE + 0)
    btbb_users = 0;
#endif
    field(0x600AF018u, 3u, saved_lp);
    field(0x600AF00Cu, 0xffffu, saved_lp_clock);
    field(0x600AF020u, 0x00660000u, saved_lp_map);
    field(0x600B2800u, 1u << 24, saved_rng);
    if (tiku_drv_phy_c5_off() != 0) {
        tiku_c5_fatal("radio PHY shutdown failed");
    }
    prepared = inited = 0;
    if (phy_lock) {
        espw_osi_funcs._mutex_delete(phy_lock);
        phy_lock = NULL;
    }
}
void espw_modem_init(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    field(0x600AF00Cu, 0xffffu, 1u);
    field(0x600AF018u, 3u, 3u);
    field(0x600AF020u, 0x00660000u, 0x00660000u);
    TIKU_C5_REG_WRITE(0x600B00DCu, 1u << 31);
    TIKU_C5_REG_WRITE(0x600B00D0u, 1u << 28);
    TIKU_C5_IRQ_RESTORE(state);
    phy_lock = espw_osi_funcs._recursive_mutex_create();
    if (!phy_lock) {
        tiku_c5_fatal("radio PHY lock allocation");
    }
}
void espw_modem_wifi_clock_on(void)
{
    field(0x600A9C14u, 0x7ffu, 0x7ffu);
}
void espw_modem_wifi_clock_off(void)
{
    if (!inited) {
        field(0x600A9C14u, 0x7ffu, 0);
    }
}
void espw_modem_wifi_inited(int on)
{
    inited = on != 0;
    if (on) {
        espw_modem_wifi_clock_on();
    }
}
void espw_modem_wifi_reset(void)
{
    field(0x600A9C10u, 1u << 9, 1u << 9);
    field(0x600A9C10u, 1u << 9, 0);
}
void espw_phy_enable(void)
{
    if (!prepared) {
        tiku_c5_fatal("vendor PHY callback before preparation");
    }
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    phy_used |= 1u;
    phy_param_track_tot(1, (phy_used & 6u) != 0);
    phy_wifi_enable_set(1);
    /* Reference MAC configuration disables the baseband's RX idle watchdog. */
    set_bb_wdg(true, false, 0x18, 0xaa, false, false, false);
    espw_timer_setfn(track_timer, (void *)track, NULL);
    espw_timer_arm_us(track_timer, 1000000u, true);
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
void espw_phy_disable(void)
{
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    phy_wifi_enable_set(0);
    phy_used &= (uint8_t)~1u;
    if (!phy_used) {
        espw_timer_disarm(track_timer);
    }
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
int espw_phy_cal_result(uint32_t *us, int *fresh)
{
    *us = tiku_drv_phy_c5_calibration_us();
    *fresh = reported_calibrations != tiku_drv_phy_c5_calibrations();
    reported_calibrations = tiku_drv_phy_c5_calibrations();
    return tiku_drv_phy_c5_result();
}
int espw_read_mac(uint8_t *mac, unsigned type)
{
    if (!mac || type > ESP_MAC_BT || tiku_cpu_c5_unique_id(mac, 6) != 6) {
        return ESP_FAIL;
    }
    if (type == ESP_MAC_WIFI_SOFTAP) {
        mac[0] |= 2u;
    }
    if (type == ESP_MAC_BT) {
        mac[5] = (uint8_t)(mac[5] + 2u);
    }
    return ESP_OK;
}

#if (TIKU_DRV_BLE_ESP_ENABLE + 0)
/* MODEM_SYSCON CLK_CONF: ETM (22), the 15.4 APB (23) and MAC (24), the
 * modem security blocks and their APB (25..29), the BLE timer (30);
 * CLK_CONF1: the BT APB (16), the baseband (17), the BLE MAC (18).  BLE and
 * 15.4 share ETM, the security APB, the BT APB and the baseband; a shared
 * bit stays on while either MAC holds it.  The 15.4 clock domain's gate map
 * (CLK_CONF_POWER_ST bits 11:8) opens in the active ICG code, 2. */
#define BT_SYS_CLOCKS 0x7e400000u
#define BT_MAC_CLOCKS 0x00070000u
#define ZB_SYS_CLOCKS ((1u << 22) | (1u << 23) | (1u << 24) | (1u << 28))
#define ZB_MAC_CLOCKS ((1u << 16) | (1u << 17))
#define ZB_MAP_MASK   (0xFu << 8)
#define ZB_MAP        (1u << 10)
#define BT_RESET      0x6e018000u
static uint32_t held_sys, held_mac, held_map, bt_lp, bt_enable;
static uint8_t bt_clocked, zb_clocked;

/* The clock and map fields as the MACs hold them: a bit of a MAC that is up
 * is on, every other bit is what the registers held before the first MAC
 * came up.  Interrupts are masked by the caller. */
static void modem_apply(void)
{
    uint32_t sys = (bt_clocked ? BT_SYS_CLOCKS : 0u) |
                   (zb_clocked ? ZB_SYS_CLOCKS : 0u);
    uint32_t mac = (bt_clocked ? BT_MAC_CLOCKS : 0u) |
                   (zb_clocked ? ZB_MAC_CLOCKS : 0u);
    field(0x600A9C04u, BT_SYS_CLOCKS | ZB_SYS_CLOCKS, held_sys | sys);
    field(0x600A9C14u, BT_MAC_CLOCKS | ZB_MAC_CLOCKS, held_mac | mac);
    field(0x600A9C0Cu, ZB_MAP_MASK, zb_clocked ? ZB_MAP : held_map);
}

/** @brief Record the fields before the first MAC's clocks open. */
static void modem_hold(void)
{
    if (!bt_clocked && !zb_clocked) {
        held_sys = TIKU_C5_REG_READ(0x600A9C04u) & ~(BT_SYS_CLOCKS | ZB_SYS_CLOCKS);
        held_mac = TIKU_C5_REG_READ(0x600A9C14u) & ~(BT_MAC_CLOCKS | ZB_MAC_CLOCKS);
        held_map = TIKU_C5_REG_READ(0x600A9C0Cu) & ZB_MAP_MASK;
    }
}

void espw_modem_bt_on(void)
{
    uint32_t xtal, state;
    if (bt_clocked) {
        return;
    }
    xtal = espw_c5_xtal_mhz();
    if (xtal != 40u && xtal != 48u) {
        tiku_c5_fatal("BLE crystal unsupported");
    }
    state = TIKU_C5_IRQ_SAVE();
    modem_hold();
    bt_lp = TIKU_C5_REG_READ(0x600AF004u);
    bt_enable = TIKU_C5_REG_READ(0x600AF018u);
    bt_clocked = 1;
    modem_apply();
    field(0x600A9C10u, BT_RESET, BT_RESET);
    field(0x600A9C10u, BT_RESET, 0);
    field(0x600AF004u, 0xffffu, ((xtal * 10u - 1u) << 4) | 4u);
    field(0x600AF018u, 8u, 8u);
    TIKU_C5_IRQ_RESTORE(state);
}

void espw_modem_bt_off(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (bt_clocked) {
        field(0x600AF004u, 0xffffu, bt_lp);
        field(0x600AF018u, 8u, bt_enable);
        bt_clocked = 0;
        modem_apply();
    }
    TIKU_C5_IRQ_RESTORE(state);
}

uint32_t espw_modem_bt_lp_hz(void)
{
    return 100000u;
}

void espw_modem_154_on(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (!zb_clocked) {
        modem_hold();
        zb_clocked = 1;
        modem_apply();
    }
    TIKU_C5_IRQ_RESTORE(state);
}

void espw_modem_154_off(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (zb_clocked) {
        zb_clocked = 0;
        modem_apply();
    }
    TIKU_C5_IRQ_RESTORE(state);
}

/* The PHY in use by one of the 2.4 GHz MACs (bit 2 BLE, 4 15.4): the
 * tracker follows both, and runs while any user remains. */
static void phy_mac_enable(uint8_t bit)
{
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    phy_used |= bit;
    phy_param_track_tot((phy_used & 1u) != 0, 1);
    espw_timer_setfn(track_timer, (void *)track, NULL);
    espw_timer_arm_us(track_timer, 1000000u, true);
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}

static void phy_mac_disable(uint8_t bit)
{
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    phy_used &= (uint8_t)~bit;
    if (!phy_used) {
        espw_timer_disarm(track_timer);
    }
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}

void espw_phy_bt_enable(void)
{
    if (!prepared || !bt_clocked) {
        tiku_c5_fatal("BLE PHY without clocks");
    }
    phy_mac_enable(2u);
}

void espw_phy_bt_disable(void)
{
    phy_mac_disable(2u);
}

void espw_phy_154_enable(void)
{
    if (!prepared || !zb_clocked) {
        tiku_c5_fatal("15.4 PHY without clocks");
    }
    phy_mac_enable(4u);
}

void espw_phy_154_disable(void)
{
    phy_mac_disable(4u);
}

/* The baseband serves BLE and 15.4 and is initialised once for both; the
 * library has no shutdown, so the count only keeps a second user from
 * initialising it under the first.  The PHY's release zeroes the count. */
void espw_btbb_enable(void)
{
    if (!btbb_users++) {
        bt_bb_v2_init_cmplx(1u);
    }
}

void espw_btbb_disable(void)
{
    if (btbb_users) {
        btbb_users--;
    }
}
#endif

