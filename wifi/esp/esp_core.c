/*
 * Tiku Drivers - ESP32-C61 radios, what both stand on
 *
 * The heap the libraries allocate from, the modem's clock gating, the OS
 * adapter's timer service and, with both radios built, the coexistence
 * arbiter come up with the first radio and go down after the last.  With
 * both, one SRAM heap serves the two, sized for them at once.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <kernel/memory/tiku_mem.h>
#if defined(PLATFORM_ESP32C5)
#include <arch/esp32c5/tiku_psram_arch.h>
#include "esp_arch.h"
#else
#include <arch/esp32c61/tiku_psram_arch.h>
#endif
#include "esp_heap.h"
#include "esp_port.h"

static uint8_t      core_radios;
static uint8_t      core_heap_taken;
static uint8_t      core_ext_taken;
static uint8_t      core_osi_started;
static tiku_arena_t core_heap_arena;
static tiku_arena_t core_ext_arena;

/* Both radios at once.  C61: Wi-Fi, its packet buffers in PSRAM, peaks at
 * 22 KB of SRAM, BLE at 33 KB.  C5: Wi-Fi's buffers stay in SRAM and a scan
 * of both bands holds every record it heard, 64 KB alone; BLE 40 KB. */
#ifndef TIKU_DRV_ESP_COEX_HEAP_BYTES
#if defined(PLATFORM_ESP32C5)
#define TIKU_DRV_ESP_COEX_HEAP_BYTES (96U * 1024U)
#else
#define TIKU_DRV_ESP_COEX_HEAP_BYTES (60U * 1024U)
#endif
#endif

static const char *core_name(uint8_t radio) {
    return radio == ESPW_RADIO_BLE ? "BLE" : "Wi-Fi";
}

/** @brief Packet buffers' block from the PSRAM tier, bringing PSRAM up if
 *         it is not yet; without it they come from SRAM, as before. */
static void core_ext_take(uint32_t bytes) {
    tiku_mem_request_t req = TIKU_MEM_REQUEST_DEFAULT;

#if defined(PLATFORM_ESP32C5)
    if (tiku_c5_psram_attach() != 0) {
#else
    if (tiku_esp32c61_psram_attach() != TIKU_ESP32C61_PSRAM_OK) {
#endif
        TIKU_PRINTF("[esp] no PSRAM: packet buffers come from SRAM\n");
        return;
    }
    req.alignment = 8U;
    req.allocation_class = TIKU_MEM_TRANSIENT;
    if (tiku_tier_arena_create_opts(&core_ext_arena, TIKU_MEM_PSRAM, bytes, 0U,
                                    &req) != TIKU_MEM_OK) {
        TIKU_PRINTF("[esp] no %lu KB in the PSRAM tier: packet buffers come "
                    "from SRAM\n", (unsigned long)(bytes / 1024U));
        return;
    }
    espw_heap_ext_init(core_ext_arena.buf, core_ext_arena.capacity);
    core_ext_taken = 1U;
}

int espw_core_up(uint8_t radio, uint32_t heap_bytes, uint32_t ext_bytes) {
    tiku_mem_request_t req = TIKU_MEM_REQUEST_DEFAULT;

    if (core_radios == 0U && core_osi_started) {
        TIKU_PRINTF("[esp] previous radio workers still active; restart refused\n");
        return -1;
    }
    if (core_radios != 0U) {
        /* The other radio is up: everything below stands already. */
        if (ext_bytes != 0U && !core_ext_taken) {
            core_ext_take(ext_bytes);
        }
        core_radios |= radio;
        return 0;
    }
#if ESPW_COEX
    heap_bytes = TIKU_DRV_ESP_COEX_HEAP_BYTES;
#endif
    if (!core_heap_taken) {
        req.alignment = 8U;
        req.allocation_class = TIKU_MEM_TRANSIENT;
        if (tiku_mem_workspace_open(&core_heap_arena, heap_bytes, &req) !=
            TIKU_MEM_OK) {
            TIKU_PRINTF("[esp] no %lu KB for the %s heap in the SRAM tier\n",
                        (unsigned long)(heap_bytes / 1024U),
                        core_name(radio));
            return -1;
        }
        espw_heap_init(core_heap_arena.buf, core_heap_arena.capacity);
        core_heap_taken = 1U;
    }
    if (ext_bytes != 0U && !core_ext_taken) {
        core_ext_take(ext_bytes);
    }
#if defined(PLATFORM_ESP32C5)
    if (espw_c5_radio_prepare() != 0) {
        core_radios = radio;
        espw_core_down(radio);
        return -1;
    }
#endif
    espw_modem_init();
    core_osi_started = 1U;
    if (espw_osi_start() != 0) {
        core_radios = radio;
        espw_core_down(radio);
        return -1;
    }
#if ESPW_COEX
    espw_coex_start();
#endif
    core_radios = radio;
    return 0;
}

void espw_core_down(uint8_t radio) {
    int ended;

    core_radios &= (uint8_t)~radio;
    if (core_radios != 0U) {
        return;
    }
    ended = !core_osi_started || espw_osi_stop() == 0;
    if (ended) { core_osi_started = 0U; }
#if defined(PLATFORM_ESP32C5)
    if (ended) { espw_c5_radio_release(); }
#endif
    if (ended && espw_heap_used() == 0U) {
        espw_heap_reset();
        (void)tiku_mem_workspace_close(&core_heap_arena);
        core_heap_taken = 0U;
    } else {
        TIKU_PRINTF("[esp] heap kept: %lu bytes still in use\n",
                    (unsigned long)espw_heap_used());
    }
    if (core_ext_taken && ended && espw_heap_ext_used() == 0U) {
        espw_heap_ext_reset();
        (void)tiku_mem_workspace_close(&core_ext_arena);
        core_ext_taken = 0U;
    } else if (core_ext_taken) {
        TIKU_PRINTF("[esp] PSRAM heap kept: %lu bytes still in use\n",
                    (unsigned long)espw_heap_ext_used());
    }
}

uint8_t espw_core_radios(void) {
    return core_radios;
}
