/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * phy_s31.c - S31 crystal-based runtime and modem PHY calibration lifecycle.
 * Register sequence and parameters follow ESP-IDF 4d59230's S31 HAL.
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "phy_s31.h"
#include "wifi_s31_arch.h"
#include "../esp_abi.h"
#include <kernel/threads/tiku_thread.h>
#include <string.h>
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
static const esp_phy_init_data_t init = {{1, 0, [2 ... 19] = 40, [127] = 0x51}};
static esp_phy_calibration_data_t calibration;
static const struct { uint32_t address, mask; } clocks[] = {
    {0x20589000, 1u<<30}, {0x2070401c, 0x11000000}, {0x205871e0, 0x3f},
    {0x20704014, 3u<<30}, {0x20109c0c, 0x64646000}, {0x2010f020, 0x66660000},
    {0x20109c04, 1u<<12}, {0x2010f018, 7}, {0x2010f00c, 0xffff},
    {0x20109c14, 0x38e5ff}
};
static uint32_t saved[sizeof clocks / sizeof clocks[0]], calibration_us;
static unsigned active, faulted, changing, calibrated;
extern void ets_update_cpu_frequency(unsigned mhz);
/** @brief Serialize foreground RF operations while retaining IRQ delivery. */
static int enter(void)
{
    uint32_t state = espw_arch_mie_off();
    int ok = (state & 8u) && !tiku_s31_in_trap && tiku_thread_in_kernel() && !changing;
    if (ok) changing = 1;
    espw_arch_mie_restore(state);
    return ok;
}
/** @brief Restore the acquired digital fields after RF has stopped. */
static void restore(void)
{
    uint32_t state = espw_arch_mie_off();
    for (unsigned i = 0; i < sizeof clocks / sizeof clocks[0]; i++)
        REG(clocks[i].address) = (REG(clocks[i].address) & ~clocks[i].mask) | saved[i];
    REG(0x20704104) = 1u<<31; REG(0x207040f8) = 1u<<28;
    espw_arch_mie_restore(state);
}
int tiku_drv_phy_s31_mac(uint8_t mac[6])
{
    if (!mac) return -1;
    uint32_t lo = REG(0x20715050), hi = REG(0x20715054);
    mac[0] = hi >> 8; mac[1] = hi; mac[2] = lo >> 24;
    mac[3] = lo >> 16; mac[4] = lo >> 8; mac[5] = lo;
    return (mac[0] & 1u) || (!lo && !(hi & 0xffffu)) ? -1 : 0;
}
int tiku_drv_phy_s31_on(void)
{
    if (!enter()) return -1;
    if (active || faulted) { changing = 0; return -1; }
    for (unsigned i = 0; i < sizeof clocks / sizeof clocks[0]; i++)
        saved[i] = REG(clocks[i].address) & clocks[i].mask;
    if (!calibrated) {
        memset(&calibration, 0, sizeof calibration);
        if (tiku_drv_phy_s31_mac(calibration.mac)) { changing = 0; return -1; }
    }
    ets_update_cpu_frequency(40);
    REG(0x207040f4) = 0x11100000; REG(0x20589000) |= 1u<<30;
    REG(0x2070401c) |= 0x11000000;
    REG(0x205871e0) = 0x15; REG(0x205871e0) = 0x1d;
    REG(0x207040f0) = (1u<<28) | (1u<<29);
    REG(0x20704014) = (REG(0x20704014) & ~(3u<<30)) | (2u<<30);
    REG(0x20109c0c) |= 0x64646000; REG(0x2010f020) |= 0x66660000;
    REG(0x20704104) = 1u<<31; REG(0x207040f8) = 1u<<28;
    REG(0x20109c04) |= 1u<<12; REG(0x2010f018) |= 7;
    REG(0x2010f00c) = (REG(0x2010f00c) & ~0xffffu) | 1;
    REG(0x20109c10) |= 1u<<8; REG(0x20109c10) &= ~(1u<<8);
    REG(0x20109c14) |= 0x38e5ff;
    if ((REG(0x20109c14) & 0x38e5ff) != 0x38e5ff ||
        (REG(0x2010f018) & 7u) != 7u || (REG(0x205871e0) & 0x3f) != 0x1d) {
        restore(); changing = 0; return -1;
    }
    if (!calibrated) {
        uint64_t start = tiku_s31_time();
        int result = register_chipv7_phy(&init, &calibration, PHY_RF_CAL_FULL);
        calibration_us = (uint32_t)((tiku_s31_time() - start) / 16u);
        if (result != 0 && result != 1) {
            faulted = 1; changing = 0; return -1;
        }
        calibrated = 1;
    } else {
        phy_wakeup_init();
    }
    active = 1; changing = 0;
    return 0;
}
int tiku_drv_phy_s31_off(void)
{
    if (!enter()) return -1;
    if (!active || faulted) { changing = 0; return -1; }
    phy_close_rf(); phy_xpd_tsens(); phy_wait_freq_hw_hop_done();
    restore(); active = 0; changing = 0;
    return 0;
}
int tiku_drv_phy_s31_active(void) { return active; }
uint32_t tiku_drv_phy_s31_calibration_us(void) { return calibration_us; }
/** @brief The Korvo module's crystal frequency in MHz. */
uint32_t rtc_clk_xtal_freq_get(void) { return 40; }
