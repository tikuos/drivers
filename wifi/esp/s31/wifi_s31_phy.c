/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_s31_phy.c - Native S31 station PHY, DMA permissions and clock ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_s31_arch.h"
#include "phy_s31.h"
#include "../esp_port.h"
#include <arch/esp32s31/tiku_trng_arch.h>
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
static uint32_t saved_trust, saved_wifi_clock, track_timer[5];
static void *phy_lock;
static unsigned prepared, inited, wifi_on, fresh;
/** @brief Track PHY parameters from the radio timer worker. */
static void track(void *arg)
{
    (void)arg;
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    if (wifi_on) phy_param_track_tot(1, 0);
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
int espw_s31_radio_prepare(void)
{
    if (prepared || tiku_drv_phy_s31_active()) return -1;
    saved_trust = REG(0x2050400cu);
    if ((saved_trust & 4u) && (saved_trust & 3u)) return -1;
    if (tiku_s31_trng_init()) return -1;
    saved_wifi_clock = REG(0x20109c14u) & 0x7ffu;
    fresh = tiku_drv_phy_s31_calibration_us() == 0;
    if (tiku_drv_phy_s31_on()) return -1;
    /* Wi-Fi DMA master 3 requires access to the SRAM descriptor and buffer pool. */
    REG(0x2050400cu) = saved_trust & ~3u;
    if (REG(0x2050400cu) & 3u) {
        (void)tiku_drv_phy_s31_off(); return -1;
    }
    prepared = 1;
    return 0;
}
void espw_s31_radio_release(void)
{
    if (!prepared) return;
    if (wifi_on) tiku_s31_fatal("PHY release while Wi-Fi active");
    if (tiku_drv_phy_s31_off()) tiku_s31_fatal("PHY shutdown");
    REG(0x20109c14u) = (REG(0x20109c14u) & ~0x7ffu) | saved_wifi_clock;
    REG(0x2050400cu) = saved_trust;
    if (phy_lock) espw_osi_funcs._mutex_delete(phy_lock);
    phy_lock = NULL; prepared = inited = 0;
}
void espw_modem_init(void)
{
    phy_lock = espw_osi_funcs._recursive_mutex_create();
    if (!phy_lock) tiku_s31_fatal("PHY lock allocation");
}
void espw_modem_wifi_clock_on(void) { REG(0x20109c14u) |= 0x7ffu; }
void espw_modem_wifi_clock_off(void)
{ if (!inited && !prepared) REG(0x20109c14u) &= ~0x7ffu; }
void espw_modem_wifi_inited(int on)
{ inited = on != 0; if (on) espw_modem_wifi_clock_on(); }
void espw_modem_wifi_reset(void)
{ REG(0x20109c10u) |= 1u<<9; REG(0x20109c10u) &= ~(1u<<9); }
void espw_phy_enable(void)
{
    if (!prepared) tiku_s31_fatal("Wi-Fi PHY without owner");
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    wifi_on = 1;
    phy_param_track_tot(1, 0); phy_wifi_enable_set(1);
    set_bb_wdg(true, false, 0x18, 0xaa, false, false, false);
    espw_timer_setfn(track_timer, (void *)track, NULL);
    espw_timer_arm_us(track_timer, 1000000u, true);
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
void espw_phy_disable(void)
{
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    phy_wifi_enable_set(0); wifi_on = 0;
    espw_timer_disarm(track_timer);
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
int espw_phy_cal_result(uint32_t *us, int *is_fresh)
{
    *us = tiku_drv_phy_s31_calibration_us(); *is_fresh = fresh;
    fresh = 0; return 0;
}
int espw_read_mac(uint8_t *mac, unsigned type)
{
    if (type > ESP_MAC_BT || tiku_drv_phy_s31_mac(mac)) return -1;
    mac[5] += type;
    return 0;
}
int espw_arch_xip_ok(void)
{
    uintptr_t address = (uintptr_t)esp_wifi_init_internal;
    return address >= 0x40020020u && address <
#if TIKU_S31_BLE_ENABLE
        0x40120000u;
#else
        0x400a0000u;
#endif
}
