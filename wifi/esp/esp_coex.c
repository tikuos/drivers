/*
 * Tiku Drivers - ESP32-C61 and ESP32-C5 radios, coexistence: Wi-Fi and BLE
 * on one RF
 *
 * Espressif's arbiter -- most of it in the chip's ROM, the rest in
 * libcoexist.a -- grants the shared radio to one stack at a time.  It runs on
 * this adapter, the shim's semaphores, timers and heap, registered once per
 * boot (the ROM keeps the pointer; its one lock sits in the static lock
 * pool and its function table in a static pool, so the heap can empty
 * between sessions).  Wi-Fi reaches it through
 * its OS table's coex entries, BLE through its coexistence hooks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <hal/tiku_printf_hal.h>

#include "esp_arch.h"
#include "esp_coex_abi.h"
#include "esp_heap.h"
#include "esp_port.h"

#define ESP_ERR_NOT_SUPPORTED   0x106

static int32_t coexa_take_from_isr(void *semphr, void *hptw) {
    if (hptw != NULL) {
        *(int32_t *)hptw = 0;           /* a wake pends the switch itself */
    }
    return espw_osi_funcs._semphr_take(semphr, 0U);
}

static int32_t coexa_give_from_isr(void *semphr, void *hptw) {
    if (hptw != NULL) {
        *(int32_t *)hptw = 0;
    }
    return espw_osi_funcs._semphr_give(semphr);
}

static int coexa_in_isr(void) {
    return espw_arch_in_isr();
}

static void coexa_nothing(void) {
}

static bool coexa_true(void) {
    return true;
}

static int coexa_no_debug(int event, int signal, bool rev) {
    (void)event;
    (void)signal;
    (void)rev;
    return ESP_ERR_NOT_SUPPORTED;
}

static int coexa_xtal_mhz(void) {
    return (int)ESPW_ARCH_XTAL_MHZ;
}

/* The arbiter's pre-init allocates its function table once per boot and
 * the ROM keeps the pointer: that comes from here, not the radio heap,
 * which empties between sessions.  Later allocations use the heap. */
static uint64_t coexa_boot_pool[16];
static size_t   coexa_boot_used;
static uint8_t  coexa_booting;

static void *coexa_malloc(size_t n) {
    if (coexa_booting) {
        size_t need = (n + 7U) & ~(size_t)7U;
        void *p = NULL;

        if (coexa_boot_used + need <= sizeof coexa_boot_pool) {
            p = (uint8_t *)coexa_boot_pool + coexa_boot_used;
            coexa_boot_used += need;
        }
        return p;
    }
    return espw_malloc(n);
}

static void coexa_free(void *p) {
    uintptr_t a = (uintptr_t)p, base = (uintptr_t)coexa_boot_pool;

    if (a >= base && a < base + sizeof coexa_boot_pool) {
        return;                         /* the boot pool is never given back */
    }
    espw_free(p);
}

static espw_coex_adapter_t espw_coex_adapter = {
    ._version = ESPW_COEX_ADAPTER_VERSION,
    ._task_yield_from_isr = coexa_nothing,
    ._semphr_create = NULL,             /* filled from the OS table below */
    ._semphr_delete = NULL,
    ._semphr_take_from_isr = coexa_take_from_isr,
    ._semphr_give_from_isr = coexa_give_from_isr,
    ._semphr_take = NULL,
    ._semphr_give = NULL,
    ._is_in_isr = coexa_in_isr,
    ._malloc_internal = coexa_malloc,
    ._free = coexa_free,
    ._esp_timer_get_time = espw_time_us,
    ._env_is_chip = coexa_true,
    ._timer_disarm = espw_timer_disarm,
    ._timer_done = NULL,
    ._timer_setfn = espw_timer_setfn,
    ._timer_arm_us = espw_timer_arm_us,
    ._debug_matrix_init = coexa_no_debug,
    ._xtal_freq_get = coexa_xtal_mhz,
    ._magic = (int32_t)ESPW_COEX_ADAPTER_MAGIC,
};

void espw_coex_start(void) {
    static uint8_t registered;
    esp_err_t rc;

    if (registered) {
        return;
    }
    espw_coex_adapter._semphr_create = espw_osi_funcs._semphr_create;
    espw_coex_adapter._semphr_delete = espw_osi_funcs._semphr_delete;
    espw_coex_adapter._semphr_take = espw_osi_funcs._semphr_take;
    espw_coex_adapter._semphr_give = espw_osi_funcs._semphr_give;
    espw_coex_adapter._timer_done = espw_osi_funcs._timer_done;
    coexa_booting = 1U;
    rc = esp_coex_adapter_register(&espw_coex_adapter);
    if (rc == ESP_OK) {
        rc = coex_pre_init();
    }
    coexa_booting = 0U;
    if (rc != ESP_OK) {
        TIKU_PRINTF("[esp] coexistence: 0x%lx\n", (unsigned long)rc);
        return;
    }
    registered = 1U;
    TIKU_PRINTF("[esp] coexistence %s\n", coex_version_get());
}

/* The library prints through this, formatted here as the others are. */
int coexist_printf(const char *fmt, ...) {
    char line[128];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    TIKU_PRINTF("%s", line);
    return n;
}
