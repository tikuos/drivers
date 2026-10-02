/*
 * Tiku Drivers - ESP32-C61 radios, what both stand on
 *
 * The heap the libraries allocate from, the modem's clock gating and the OS
 * adapter's timer service come up with a radio and go down after it.  Wi-Fi
 * and BLE together need the coexistence arbiter, not here yet: until then
 * one radio at a time, and the second is told so.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <kernel/memory/tiku_mem.h>
#include "esp_heap.h"
#include "esp_port.h"

static uint8_t      core_radios;
static uint8_t      core_heap_taken;
static tiku_arena_t core_heap_arena;

static const char *core_name(uint8_t radio) {
    return radio == ESPW_RADIO_BLE ? "BLE" : "Wi-Fi";
}

int espw_core_up(uint8_t radio, uint32_t heap_bytes) {
    tiku_mem_request_t req = TIKU_MEM_REQUEST_DEFAULT;

    if (core_radios != 0U) {
        TIKU_PRINTF("[esp] %s is up: turn it off first -- Wi-Fi and BLE "
                    "together need coexistence, not here yet\n",
                    core_name(core_radios));
        return -1;
    }
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
    espw_modem_init();
    if (espw_osi_start() != 0) {
        core_radios = radio;
        espw_core_down(radio);
        return -1;
    }
    core_radios = radio;
    return 0;
}

void espw_core_down(uint8_t radio) {
    int ended;

    core_radios &= (uint8_t)~radio;
    if (core_radios != 0U) {
        return;
    }
    ended = espw_osi_stop() == 0;
    if (ended && espw_heap_used() == 0U) {
        espw_heap_reset();
        (void)tiku_mem_workspace_close(&core_heap_arena);
        core_heap_taken = 0U;
    } else {
        TIKU_PRINTF("[esp] heap kept: %lu bytes still in use\n",
                    (unsigned long)espw_heap_used());
    }
}

uint8_t espw_core_radios(void) {
    return core_radios;
}
