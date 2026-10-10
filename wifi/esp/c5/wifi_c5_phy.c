/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_c5_phy.c - C5 Wi-Fi clock groups over a foreground-owned PHY lifecycle.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_c5_arch.h"
#include "tiku_drv_phy_c5.h"
#include "../esp_port.h"
#include <arch/esp32c5/tiku_cpu_common.h>
#include <arch/esp32c5/tiku_timer_arch.h>
#include <arch/esp32c5/tiku_esp32c5_regs.h>

static uint32_t saved_lp, saved_lp_clock, saved_lp_map, saved_rng;
static uint32_t track_timer[5];
static uint8_t prepared, inited, phy_used;
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
        phy_param_track_tot((phy_used & 1u) != 0, (phy_used & 2u) != 0);
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
    phy_param_track_tot(1, (phy_used & 2u) != 0);
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
#define BT_SYS_CLOCKS 0x7e400000u
#define BT_MAC_CLOCKS 0x00070000u
#define BT_RESET      0x6e018000u
static uint32_t bt_sys, bt_mac, bt_lp, bt_enable;
static uint8_t bt_clocked;

void espw_modem_bt_on(void)
{
    uint32_t xtal, state;
    if (bt_clocked) {
        return;
    }
    xtal = (TIKU_C5_REG_READ(0x60096110u) >> 24) & 127u;
    if (xtal != 40u && xtal != 48u) {
        tiku_c5_fatal("BLE crystal unsupported");
    }
    state = TIKU_C5_IRQ_SAVE();
    bt_sys = TIKU_C5_REG_READ(0x600A9C04u);
    bt_mac = TIKU_C5_REG_READ(0x600A9C14u);
    bt_lp = TIKU_C5_REG_READ(0x600AF004u);
    bt_enable = TIKU_C5_REG_READ(0x600AF018u);
    field(0x600A9C04u, BT_SYS_CLOCKS, BT_SYS_CLOCKS);
    field(0x600A9C14u, BT_MAC_CLOCKS, BT_MAC_CLOCKS);
    field(0x600A9C10u, BT_RESET, BT_RESET);
    field(0x600A9C10u, BT_RESET, 0);
    field(0x600AF004u, 0xffffu, ((xtal * 10u - 1u) << 4) | 4u);
    field(0x600AF018u, 8u, 8u);
    bt_clocked = 1;
    TIKU_C5_IRQ_RESTORE(state);
}
void espw_modem_bt_off(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (bt_clocked) {
        field(0x600AF004u, 0xffffu, bt_lp);
        field(0x600AF018u, 8u, bt_enable);
        field(0x600A9C14u, BT_MAC_CLOCKS, bt_mac);
        field(0x600A9C04u, BT_SYS_CLOCKS, bt_sys);
        bt_clocked = 0;
    }
    TIKU_C5_IRQ_RESTORE(state);
}
uint32_t espw_modem_bt_lp_hz(void)
{
    return 100000u;
}
void espw_phy_bt_enable(void)
{
    if (!prepared || !bt_clocked) {
        tiku_c5_fatal("BLE PHY without clocks");
    }
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    phy_used |= 2u;
    phy_param_track_tot((phy_used & 1u) != 0, 1);
    espw_timer_setfn(track_timer, (void *)track, NULL);
    espw_timer_arm_us(track_timer, 1000000u, true);
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
void espw_phy_bt_disable(void)
{
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    phy_used &= (uint8_t)~2u;
    if (!phy_used) {
        espw_timer_disarm(track_timer);
    }
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
#endif
