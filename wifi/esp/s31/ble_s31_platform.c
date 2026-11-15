/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * ble_s31_platform.c - S31 BLE modem clocks and native runtime callbacks.
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_bt.h"
#include "esp_log.h"
#include "btdm_lp.h"
#include <stdarg.h>
#include <stdio.h>
#include <arch/esp32s31/tiku_runtime_arch.h>

void s31_bt_log(const char *format, ...)
{
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    tiku_s31_puts(message);
}

/* Hardware reset resets the modem; explicit radio shutdown uses controller_disable. */
int esp_register_shutdown_handler(void (*fn)(void)) { return fn ? 0 : -1; }
int esp_unregister_shutdown_handler(void (*fn)(void)) { return fn ? 0 : -1; }
#include "modem/modem_syscon_struct.h"
#include "modem/modem_lpcon_struct.h"
#include "btdm_osal.h"
#define SYSCON ((modem_syscon_dev_t *)0x20109c00u)
#define LPCON ((modem_lpcon_dev_t *)0x2010f000u)
static uint32_t clocks0, clocks1, lp0, lp1;
static unsigned clock_live;
extern int r_btdm_hal_rtc_freq_set(uint64_t);
extern void bt_bb_v2_init_cmplx(unsigned);
extern uint8_t espw_core_radios(void);

void btdm_lp_enable_clock(esp_bt_ctrl_btdm_config_t *cfg)
{
    (void)cfg;
    clocks0 = SYSCON->clk_conf.val;
    clocks1 = SYSCON->clk_conf1.val;
    lp0 = LPCON->lp_timer_conf.val;
    lp1 = LPCON->clk_conf.val;
    SYSCON->clk_conf.clk_etm_en = 1;
    SYSCON->clk_conf.clk_modem_sec_en = 1;
    SYSCON->clk_conf.clk_modem_sec_ecb_en = 1;
    SYSCON->clk_conf.clk_modem_sec_ccm_en = 1;
    SYSCON->clk_conf.clk_modem_sec_bah_en = 1;
    SYSCON->clk_conf.clk_modem_sec_apb_en = 1;
    SYSCON->clk_conf.clk_ble_timer_en = 1;
    SYSCON->clk_conf1.clk_bt_apb_en = 1;
    SYSCON->clk_conf1.clk_btmac_en = 1;
    SYSCON->clk_conf1.clk_btbb_en = 1;
    SYSCON->modem_rst_conf.rst_btmac = 1;
    SYSCON->modem_rst_conf.rst_btmac = 0;
    SYSCON->modem_rst_conf.rst_btmac_apb = 1;
    SYSCON->modem_rst_conf.rst_btmac_apb = 0;
    SYSCON->modem_rst_conf.rst_ble_timer = 1;
    SYSCON->modem_rst_conf.rst_ble_timer = 0;
#if TIKU_S31_RADIO_COEX
    /* Wi-Fi retains the shared security engines while it owns the radio. */
    if (!(espw_core_radios() & 1u)) {
#endif
    SYSCON->modem_rst_conf.rst_modem_ecb = 1;
    SYSCON->modem_rst_conf.rst_modem_ecb = 0;
    SYSCON->modem_rst_conf.rst_modem_ccm = 1;
    SYSCON->modem_rst_conf.rst_modem_ccm = 0;
    SYSCON->modem_rst_conf.rst_modem_bah = 1;
    SYSCON->modem_rst_conf.rst_modem_bah = 0;
    SYSCON->modem_rst_conf.rst_modem_sec = 1;
    SYSCON->modem_rst_conf.rst_modem_sec = 0;
#if TIKU_S31_RADIO_COEX
    }
#endif
    /* The 40 MHz crystal divided by 400 supplies the 100 kHz BLE timer. */
    LPCON->lp_timer_conf.val = 4u | (399u << 4);
    LPCON->clk_conf.clk_lp_timer_en = 1;
    LPCON->clk_conf.clk_coex_en = 1;
    clock_live = 1;
}

void btdm_lp_disable_clock(void)
{
    if (!clock_live) return;
#if TIKU_S31_RADIO_COEX
    /* The final radio release restores the shared modem clock snapshot. */
    if (espw_core_radios() & 1u) { clock_live = 0; return; }
#endif
    SYSCON->clk_conf.val = clocks0;
    SYSCON->clk_conf1.val = clocks1;
    LPCON->lp_timer_conf.val = lp0;
    LPCON->clk_conf.val = lp1;
    clock_live = 0;
}

int btdm_lp_init(void)
{
    r_btdm_hal_rtc_freq_set(100000);
    return 0;
}
void btdm_lp_deinit(void) { }
void btdm_lp_reset(bool enable) { if (enable) bt_bb_v2_init_cmplx(1); }
void btdm_lp_shutdown(void)
{
    SYSCON->modem_rst_conf.rst_btmac = 1;
    SYSCON->modem_rst_conf.rst_btmac = 0;
}
void e_btdm_lp_modem_clock_set(bool enable)
{
    /* Modem sleep is disabled. The owner retains clocks through controller disable. */
    (void)enable;
}

extern int __real_btdm_osal_elem_mempool_init(btdm_osal_elem_num_t *);
/** @brief Add bounded OS objects used outside the SDK's BLE object calculation. */
int __wrap_btdm_osal_elem_mempool_init(btdm_osal_elem_num_t *requested)
{
    if (!requested || requested->sem_count > UINT16_MAX - 4u ||
        requested->mutex_count > UINT16_MAX - 4u ||
        requested->evtq_count > UINT16_MAX - 2u) return -1;
    btdm_osal_elem_num_t counts = *requested;
    counts.sem_count += 4;
    counts.mutex_count += 4;
    counts.evtq_count += 2;
    return __real_btdm_osal_elem_mempool_init(&counts);
}
