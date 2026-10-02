/*
 * Tiku Drivers - ESP32-C61 Wi-Fi over Espressif's radio libraries
 *
 * The libraries' code lives in the XIP window, written as xip.bin beside the
 * boot image.  Its header records where this image's text and bss end, so a
 * flash holding another build's xip.bin is refused, never called into.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <hal/tiku_printf_hal.h>
#include "tiku_drv_wifi_esp.h"
#include "esp_abi.h"

#define ESPW_PRINTF(...) TIKU_PRINTF("[esp-wifi] " __VA_ARGS__)
#define ESPW_XIP_MAGIC   0x31504958UL           /* "XIP1" */

extern char _etext[];
extern char __bss_end[];

typedef struct {
    uint32_t magic;
    uint32_t etext;         /* this image's _etext and __bss_end: any */
    uint32_t bss_end;       /* rebuild that moves the kernel moves them */
} espw_xip_header_t;

__attribute__((section(".xip.header"), used))
static const espw_xip_header_t espw_xip_header = {
    ESPW_XIP_MAGIC, (uint32_t)(uintptr_t)_etext, (uint32_t)(uintptr_t)__bss_end
};

/* What the link must carry whole: the stack's init and all it reaches. */
__attribute__((section(".xip.roots"), used))
static void *const espw_roots[] = { (void *)esp_wifi_init_internal };

static uint8_t espw_ready;

int tiku_drv_wifi_esp_xip_ok(void) {
    /* Through a volatile view: the compiler knows the initializer, but the
     * flash may hold an older build's bytes. */
    const volatile espw_xip_header_t *h =
        (const volatile espw_xip_header_t *)&espw_xip_header;

    return h->magic == ESPW_XIP_MAGIC &&
           h->etext == (uint32_t)(uintptr_t)_etext &&
           h->bss_end == (uint32_t)(uintptr_t)__bss_end;
}

static int espw_init(void) {
    (void)espw_roots;
    if (!tiku_drv_wifi_esp_xip_ok()) {
        ESPW_PRINTF("xip.bin in flash is not this build's -- make flash "
                    "writes both images\n");
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    ESPW_PRINTF("libraries run from flash: phy %s, net80211 %s, pp %s\n",
                get_phy_version_str(), libnet80211_reversion_git,
                libpp_reversion_git);
    espw_ready = 1U;
    return TIKU_DRV_OK;
}

static int espw_deinit(void) {
    espw_ready = 0U;
    return TIKU_DRV_OK;
}

const tiku_drv_t tiku_drv_wifi_esp = {
    .name   = "esp-wifi",
    .class  = TIKU_DRV_CLASS_WIFI,
    .init   = espw_init,
    .deinit = espw_deinit,
};
