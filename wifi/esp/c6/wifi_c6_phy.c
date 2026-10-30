/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_c6_phy.c - C6 modem clock ownership, PHY calibration and RF shutdown.
 * Register definitions and PHY parameters: ESP-IDF 4d59230, ESP32-C6.
 * SPDX-FileCopyrightText: 2016-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_c6_arch.h"
#include "../esp_port.h"
#include "../esp_heap.h"
#if (TIKU_DRV_BLE_ESP_ENABLE + 0)
#include "../esp_ble_abi.h"
#endif
#include <arch/esp32c6/tiku_cpu_common.h>
#include <string.h>
#define SYSCON 0x600a9800u
#define LPCON 0x600af000u
#define WIFI_CLOCKS 0x7ffu
#define FE_CLOCKS ((1u << 13) | (1u << 14) | (1u << 15) | (1u << 16))
#define BT_FIELDS ((1u << 22) | (0x3fu << 25))
#define BT_RESET ((15u << 15) | (1u << 22) | (1u << 30))
#define CAL_CLOCKS ((1u << 17) | (1u << 18))
static const uint32_t registers[] = {SYSCON + 4,   SYSCON + 0x14, SYSCON + 0xc,
                                     LPCON + 0xc,  LPCON + 0x10,  LPCON + 0x18,
                                     LPCON + 0x20, 0x60098010u,   0x600b000cu};
static const uint32_t masks[] = {(1u << 22) | (1u << 28),
                                 WIFI_CLOCKS | FE_CLOCKS | CAL_CLOCKS,
                                 0xfffff000u,
                                 0xffffu,
                                 1u,
                                 7u,
                                 0xffff0000u,
                                 3u,
                                 3u << 30};
static uint32_t saved[sizeof registers / sizeof registers[0]], tracking_timer[5];
static void *phy_lock;
static uint8_t prepared, wifi_inited, active, calibrated, fresh;
#if (TIKU_DRV_BLE_ESP_ENABLE + 0)
static uint32_t bt_saved[3];
static uint8_t bt_clocked;
#endif
static uint32_t calibration_us;
static int calibration_result = -1;
/* Version 10 C6 defaults, with transmit power capped at 20 dBm. */
static const esp_phy_init_data_t parameters = {{0x0a, 0,    0x50, 0x50, 0x50, 0x50, 0x50,
                                                0x4c, 0x4c, 0x4c, 0x4c, 0x48, 0x44, 0x3c,
                                                0x3c, 0x3c, 0x4c, 0x4c, 0x4c, 0x48, [127] = 0x51}};
/** @brief Replace owned fields while retaining other peripheral owners' bits. */
static void field(uint32_t address, uint32_t mask, uint32_t value)
{
    uint32_t state = native_irq_save();
    C6_REG(address) = (C6_REG(address) & ~mask) | (value & mask);
    native_irq_restore(state);
}
void espw_modem_init(void)
{
    if (prepared)
        espw_c6_fatal("modem already prepared");
    uint32_t state = native_irq_save();
    for (unsigned i = 0; i < sizeof registers / sizeof registers[0]; i++)
        saved[i] = C6_REG(registers[i]);
    field(0x60098010u, 3u, 0);
    field(LPCON + 0x10, 1u, 1u);
    field(LPCON + 0x18, 7u, 7u);
    field(LPCON + 0xc, 0xffffu, 1u);
    C6_REG(0x600b00ccu) = (1u << 28) | (1u << 29);
    field(0x600b000cu, 3u << 30, 2u << 30);
    field(SYSCON + 0xc, 0xfffff000u,
          (6u << 12) | (4u << 16) | (6u << 20) | (4u << 24) | (6u << 28));
    field(LPCON + 0x20, 0xffff0000u, (6u << 16) | (6u << 20) | (6u << 24) | (6u << 28));
    C6_REG(0x600b00dcu) = 1u << 31;
    C6_REG(0x600b00d0u) = 1u << 28;
    prepared = 1;
    native_irq_restore(state);
    if (!phy_lock)
        phy_lock = espw_osi_funcs._recursive_mutex_create();
    if (!phy_lock)
        espw_c6_fatal("PHY lock allocation");
}
void espw_c6_radio_release(void)
{
    if (!prepared)
        return;
    if (active)
        espw_c6_fatal("PHY still active at release");
    uint32_t state = native_irq_save();
    for (unsigned i = 0; i < sizeof registers / sizeof registers[0]; i++)
        field(registers[i], masks[i], saved[i]);
    C6_REG(0x600b00dcu) = 1u << 31;
    C6_REG(0x600b00d0u) = 1u << 28;
    /* PHY calibration state lasts until CPU reset; later starts wake the RF. */
    prepared = wifi_inited = 0;
    native_irq_restore(state);
}
void espw_modem_wifi_clock_on(void)
{
    /* Coexistence pre-init leaves its clock gated; each MAC requires it. */
    field(LPCON + 0x18, 2u, 2u);
    field(SYSCON + 0x14, WIFI_CLOCKS, WIFI_CLOCKS);
}
void espw_modem_wifi_clock_off(void)
{
    if (!wifi_inited)
        field(SYSCON + 0x14, WIFI_CLOCKS, 0);
}
void espw_modem_wifi_reset(void)
{
    field(SYSCON + 0x10, 1u << 10, 1u << 10);
    field(SYSCON + 0x10, 1u << 10, 0);
}
void espw_modem_wifi_inited(int on)
{
    wifi_inited = on != 0;
}
/** @brief Run periodic PHY tracking while the radio remains active. */
static void track(void *unused)
{
    (void)unused;
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    if (active)
        phy_param_track_tot((active & 1u) != 0, (active & 2u) != 0);
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
/** @brief Acquire the calibrated RF front end for one radio. */
static void phy_enable(uint8_t radio)
{
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    if (!active) {
        tiku_c6_trng_radio_set(TIKU_C6_TRNG_RADIO_BUSY);
        field(SYSCON + 0x14, FE_CLOCKS | CAL_CLOCKS | WIFI_CLOCKS,
              FE_CLOCKS | CAL_CLOCKS | WIFI_CLOCKS);
        field(SYSCON + 4, (1u << 22) | (1u << 28), (1u << 22) | (1u << 28));
        if (!calibrated) {
            esp_phy_calibration_data_t *cal = espw_calloc(1, sizeof *cal);
            if (!cal)
                espw_c6_fatal("PHY calibration allocation");
            (void)tiku_cpu_c6_unique_id(cal->mac, sizeof cal->mac);
            phy_init_param_set(1);
            int64_t start = espw_time_us();
            calibration_result = register_chipv7_phy(&parameters, cal, PHY_RF_CAL_FULL);
            calibration_us = (uint32_t)(espw_time_us() - start);
            espw_free(cal);
            if (calibration_result != 0 && calibration_result != 1)
                espw_c6_fatal("PHY calibration result");
            calibrated = fresh = 1;
        } else {
            phy_wakeup_init();
        }
        active = radio;
        tiku_c6_trng_radio_set(TIKU_C6_TRNG_RADIO_RF);
#if (TIKU_DRV_BLE_ESP_ENABLE + 0)
        field(SYSCON + 0x14, CAL_CLOCKS, bt_clocked ? CAL_CLOCKS : 0);
        field(SYSCON + 4, (1u << 22) | (1u << 28), bt_clocked ? (1u << 22) | (1u << 28) : 0);
#else
        field(SYSCON + 0x14, CAL_CLOCKS, 0);
        field(SYSCON + 4, (1u << 22) | (1u << 28), 0);
#endif
        espw_timer_setfn(tracking_timer, (void *)track, NULL);
        espw_timer_arm_us(tracking_timer, 1000000, true);
    }
    active |= radio;
    if (radio == 1u)
        phy_wifi_enable_set(1);
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
/** @brief Release RF after its last radio has stopped. */
static void phy_disable(uint8_t radio)
{
    (void)espw_osi_funcs._mutex_lock(phy_lock);
    if (radio == 1u)
        phy_wifi_enable_set(0);
    uint8_t before = active;
    active &= (uint8_t)~radio;
    if (before && !active) {
        tiku_c6_trng_radio_set(TIKU_C6_TRNG_RADIO_BUSY);
        espw_timer_disarm(tracking_timer);
        phy_close_rf();
        phy_xpd_tsens();
        phy_wait_freq_hw_hop_done();
        active = 0;
        tiku_c6_trng_radio_set(TIKU_C6_TRNG_RADIO_OFF);
    }
    (void)espw_osi_funcs._mutex_unlock(phy_lock);
}
int espw_c6_phy_active(void)
{
    return active;
}
int espw_phy_cal_result(uint32_t *us, int *was_fresh)
{
    if (us)
        *us = calibration_us;
    if (was_fresh)
        *was_fresh = fresh;
    fresh = 0;
    return calibration_result;
}
int espw_read_mac(uint8_t *mac, unsigned type)
{
    if (!mac || type > ESP_MAC_BT)
        return ESP_FAIL;
    (void)tiku_cpu_c6_unique_id(mac, 6);
    if (type == ESP_MAC_WIFI_SOFTAP)
        mac[0] |= 2u;
    if (type == ESP_MAC_BT)
        mac[5] = (uint8_t)(mac[5] + 2u);
    return ESP_OK;
}

void espw_phy_enable(void) { phy_enable(1u); }
void espw_phy_disable(void) { phy_disable(1u); }
#if (TIKU_DRV_BLE_ESP_ENABLE + 0)
void espw_phy_bt_enable(void) { phy_enable(2u); }
void espw_phy_bt_disable(void) { phy_disable(2u); }
void espw_modem_bt_on(void)
{
    uint32_t state = native_irq_save();
    if (!bt_clocked) {
        bt_saved[0] = C6_REG(SYSCON + 4);
        bt_saved[1] = C6_REG(LPCON + 4);
        bt_saved[2] = C6_REG(LPCON + 0x18);
        bt_clocked = 1;
        field(LPCON + 0x18, 2u, 2u);
        field(SYSCON + 4, BT_FIELDS, BT_FIELDS);
        field(SYSCON + 0x14, CAL_CLOCKS, CAL_CLOCKS);
        field(SYSCON + 0x10, BT_RESET, BT_RESET);
        field(SYSCON + 0x10, BT_RESET, 0);
        field(LPCON + 4, 0xffffu, (399u << 4) | 4u);
        field(LPCON + 0x18, 8u, 8u);
    }
    native_irq_restore(state);
}
void espw_modem_bt_off(void)
{
    uint32_t state = native_irq_save();
    if (bt_clocked) {
        field(SYSCON + 4, BT_FIELDS, bt_saved[0]);
        field(SYSCON + 0x14, CAL_CLOCKS, 0);
        field(LPCON + 4, 0xffffu, bt_saved[1]);
        field(LPCON + 0x18, 8u, bt_saved[2]);
        bt_clocked = 0;
    }
    native_irq_restore(state);
}
uint32_t espw_modem_bt_lp_hz(void) { return 100000u; }
void espw_btbb_enable(void) { bt_bb_v2_init_cmplx(1u); }
void espw_btbb_disable(void) { }
void espw_c6_bt_reset(uint8_t modules, uint8_t start)
{
#if ESPW_COEX
    extern void coex_hw_timer_set(uint8_t, uint8_t, uint8_t, uint32_t, uint32_t);
    extern void coex_hw_timer_enable(uint8_t);
    extern void coex_hw_timer_disable(uint8_t);
    if (modules == 5u) {
        if (start) {
            coex_hw_timer_set(4u, 2u, 15u, 0u, 5000u);
            coex_hw_timer_enable(4u);
        } else {
            coex_hw_timer_disable(4u);
        }
    }
#endif
    if (modules == 5u && start) {
        field(SYSCON + 0x10, (1u << 16) | (1u << 18), (1u << 16) | (1u << 18));
        field(SYSCON + 0x10, (1u << 16) | (1u << 18), 0);
    }
}
uint32_t espw_c6_revision(void)
{
    uint32_t version = C6_REG(0x600b0850u);
    return ((version >> 22) & 3u) * 100u + ((version >> 18) & 15u);
}
#endif
