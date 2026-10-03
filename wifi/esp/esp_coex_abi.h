/*
 * Tiku Drivers - ESP32-C61 coexistence library, the ABI this driver uses
 *
 * Hand-written from ESP-IDF 4d59230 (esp_coex's private headers), the tree
 * whose esp-coex-lib commit fetch.sh pins: the adapter table the library
 * runs on and the calls the two stacks make into it.  Most of the arbiter
 * lives in the C61's ROM; libcoexist.a is the part that does not.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_ESP_COEX_ABI_H_
#define TIKU_DRV_WIFI_ESP_COEX_ABI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_abi.h"

#define ESPW_COEX_ADAPTER_VERSION   0x00000002
#define ESPW_COEX_ADAPTER_MAGIC     0xDEADBEAF

/* The status bitmaps coex_status_get() takes: one bit per scheme type. */
#define ESPW_COEX_ST_WIFI           (1U << 0)

/** @brief The OS the library calls (the C61's layout: no spin locks, no
 *         slow-clock calibration). */
typedef struct {
    int32_t _version;
    void (*_task_yield_from_isr)(void);
    void *(*_semphr_create)(uint32_t max, uint32_t init);
    void (*_semphr_delete)(void *semphr);
    int32_t (*_semphr_take_from_isr)(void *semphr, void *hptw);
    int32_t (*_semphr_give_from_isr)(void *semphr, void *hptw);
    int32_t (*_semphr_take)(void *semphr, uint32_t block_time_tick);
    int32_t (*_semphr_give)(void *semphr);
    int (*_is_in_isr)(void);
    void *(*_malloc_internal)(size_t size);
    void (*_free)(void *p);
    int64_t (*_esp_timer_get_time)(void);
    bool (*_env_is_chip)(void);
    void (*_timer_disarm)(void *timer);
    void (*_timer_done)(void *ptimer);
    void (*_timer_setfn)(void *ptimer, void *pfunction, void *parg);
    void (*_timer_arm_us)(void *ptimer, uint32_t us, bool repeat);
    int (*_debug_matrix_init)(int event, int signal, bool rev);
    int (*_xtal_freq_get)(void);
    int32_t _magic;
} espw_coex_adapter_t;

#if defined(__riscv) && __riscv_xlen == 32
_Static_assert(sizeof(espw_coex_adapter_t) == 80, "coex adapter");
_Static_assert(offsetof(espw_coex_adapter_t, _magic) == 76, "coex adapter");
#endif

/* Bring-up, once, then each stack's start and stop. */
esp_err_t esp_coex_adapter_register(espw_coex_adapter_t *funcs);
esp_err_t coex_pre_init(void);
esp_err_t coex_init(void);
void coex_deinit(void);
esp_err_t coex_enable(void);
void coex_disable(void);
const char *coex_version_get(void);

/* What Wi-Fi's OS table forwards. */
uint32_t coex_status_get(uint8_t bitmap);
int coex_wifi_request(uint32_t event, uint32_t latency, uint32_t duration);
int coex_wifi_release(uint32_t event);
int coex_wifi_channel_set(uint8_t primary, uint8_t secondary);
int coex_event_duration_get(uint32_t event, uint32_t *duration);
int coex_pti_get(uint32_t event, uint8_t *pti);
void coex_schm_status_bit_clear(uint32_t type, uint32_t status);
void coex_schm_status_bit_set(uint32_t type, uint32_t status);
int coex_schm_interval_set(uint32_t interval);
uint32_t coex_schm_interval_get(void);
uint8_t coex_schm_curr_period_get(void);
void *coex_schm_curr_phase_get(void);
int coex_schm_process_restart(void);
int coex_schm_register_callback(int type, void *callback);
int coex_register_start_cb(int (*cb)(void));
void *coex_schm_get_phase_by_idx(int phase_idx);

#endif /* TIKU_DRV_WIFI_ESP_COEX_ABI_H_ */
