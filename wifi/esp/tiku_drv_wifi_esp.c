/*
 * Tiku Drivers - ESP32-C61 Wi-Fi over Espressif's radio libraries
 *
 * The libraries' code lives in the XIP window, written as xip.bin beside the
 * boot image; its header records where this image's text and bss end, so a
 * flash holding another build's xip.bin is refused, never called into.  The
 * radio is off until asked: up takes a heap from the SRAM tier and starts
 * the OS adapter and the station; down stops them and gives the heap back.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <interfaces/wireless/tiku_wireless.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_clock.h>
#include <arch/esp32c61/tiku_sleep_arch.h>
#include "tiku_drv_wifi_esp.h"
#include "esp_heap.h"
#include "esp_port.h"

#define ESPW_XIP_MAGIC   0x31504958UL           /* "XIP1" */

/* The runner's own event: the stack has finished a scan (from its task). */
#define ESPW_EV_SCAN_DONE (TIKU_EVENT_USER + 0x20U)

/* The libraries' heap, taken from the SRAM tier while the radio is up. */
#ifndef TIKU_DRV_WIFI_ESP_HEAP_BYTES
#define TIKU_DRV_WIFI_ESP_HEAP_BYTES (56U * 1024U)
#endif

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

/* What the link must carry whole: the stack's entry points and all they
 * reach. */
__attribute__((section(".xip.roots"), used))
static void *const espw_roots[] = {
    (void *)esp_wifi_init_internal, (void *)esp_wifi_set_mode,
    (void *)esp_wifi_start, (void *)esp_wifi_stop,
    (void *)esp_wifi_deinit_internal, (void *)esp_wifi_get_mac,
    (void *)register_chipv7_phy
};

static uint8_t      espw_ready;         /* xip.bin is this build's */
static uint8_t      espw_up;
static uint8_t      espw_heap_taken;
static uint8_t      espw_mac[6];
static tiku_arena_t espw_heap_arena;

/* The last scan: what it found, how long it took, whether one runs. */
static tiku_wireless_ap_t espw_aps[TIKU_WIRELESS_MAX_SCAN_RESULTS];
static uint8_t           espw_ap_count;
static uint16_t          espw_scan_found;
static volatile uint8_t  espw_scanning;
static tiku_clock_time_t espw_scan_start;
static uint32_t          espw_scan_ticks;

TIKU_PROCESS(espw_runner, "wifi-esp");

int tiku_drv_wifi_esp_xip_ok(void) {
    /* Through a volatile view: the compiler knows the initializer, but the
     * flash may hold an older build's bytes. */
    const volatile espw_xip_header_t *h =
        (const volatile espw_xip_header_t *)&espw_xip_header;

    return h->magic == ESPW_XIP_MAGIC &&
           h->etext == (uint32_t)(uintptr_t)_etext &&
           h->bss_end == (uint32_t)(uintptr_t)__bss_end;
}

/*---------------------------------------------------------------------------*/
/* Events from the libraries                                                 */
/*---------------------------------------------------------------------------*/

void espw_event(const char *base, int32_t id, const void *data, size_t len) {
    (void)data;
    (void)len;
    if (base == NULL || strcmp(base, WIFI_EVENT) != 0) {
        ESPW_PRINTF("event %s %ld\n", base != NULL ? base : "?", (long)id);
        return;
    }
    switch (id) {
    case WIFI_EVENT_SCAN_DONE:
        /* Collected by the runner: fetching the records waits on this
         * very task, so it cannot happen here. */
        (void)tiku_process_post(&espw_runner, ESPW_EV_SCAN_DONE,
            (tiku_event_data_t)(uintptr_t)(data != NULL ?
                ((const wifi_event_sta_scan_done_t *)data)->status : 1U));
        break;
    case WIFI_EVENT_STA_START:
        ESPW_PRINTF("station started\n");
        break;
    case WIFI_EVENT_STA_STOP:
        ESPW_PRINTF("station stopped\n");
        break;
    default:
        break;
    }
}

/*---------------------------------------------------------------------------*/
/* Scanning                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Every channel the country allows, actively, with default dwell. */
static void espw_scan_begin(void) {
    esp_err_t rc;

    if (!espw_up || espw_scanning) {
        return;
    }
    espw_scanning = 1U;
    espw_scan_start = tiku_clock_time();
    rc = esp_wifi_scan_start(NULL, false);
    if (rc != ESP_OK) {
        espw_scanning = 0U;
        ESPW_PRINTF("scan: 0x%lx\n", (unsigned long)rc);
    }
}

/** @brief The scan has ended: take its records into the table, then tell
 *         whoever listens, one AP_FOUND each and a SCAN_COMPLETE. */
static void espw_scan_collect(uint32_t status) {
    uint16_t n = TIKU_WIRELESS_MAX_SCAN_RESULTS, total = 0U;
    wifi_ap_record_t *recs = espw_malloc(n * sizeof *recs);

    espw_ap_count = 0U;
    (void)esp_wifi_scan_get_ap_num(&total);
    if (recs == NULL || status != 0U ||
        esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        n = 0U;
        (void)esp_wifi_clear_ap_list();
    }
    for (uint16_t i = 0U; i < n; i++) {
        tiku_wireless_ap_t *ap = &espw_aps[espw_ap_count++];
        size_t len = strnlen((const char *)recs[i].ssid, 32U);

        memset(ap, 0, sizeof *ap);
        memcpy(ap->bssid, recs[i].bssid, sizeof ap->bssid);
        memcpy(ap->ssid, recs[i].ssid, len);
        ap->ssid_len = (uint8_t)len;
        ap->rssi = recs[i].rssi;
        ap->channel = recs[i].primary;
    }
    espw_free(recs);
    espw_scan_found = total;
    espw_scan_ticks = (uint32_t)(tiku_clock_time_t)(tiku_clock_time() -
                                                    espw_scan_start);
    espw_scanning = 0U;
    ESPW_PRINTF("*** scan done -- %u AP%s in %lu ms ***\n", (unsigned)total,
                total == 1U ? "" : "s",
                (unsigned long)(espw_scan_ticks * 1000UL / TIKU_CLOCK_SECOND));
    for (uint8_t i = 0U; i < espw_ap_count; i++) {
        (void)tiku_process_post(TIKU_PROCESS_BROADCAST,
                                TIKU_WIRELESS_EVT_AP_FOUND, &espw_aps[i]);
    }
    (void)tiku_process_post(TIKU_PROCESS_BROADCAST,
                            TIKU_WIRELESS_EVT_SCAN_COMPLETE,
                            (tiku_event_data_t)(uintptr_t)espw_ap_count);
}

/** @brief The runner: starts what the interface asked for, in the kernel
 *         thread, and collects what the stack finished. */
TIKU_PROCESS_THREAD(espw_runner, ev, data)
{
    TIKU_PROCESS_BEGIN();
    for (;;) {
        TIKU_PROCESS_WAIT_EVENT();
        if (ev == TIKU_WIRELESS_EVT_SCAN_START) {
            espw_scan_begin();
        } else if (ev == ESPW_EV_SCAN_DONE && espw_scanning) {
            espw_scan_collect((uint32_t)(uintptr_t)data);
        }
    }
    TIKU_PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* Up and down                                                               */
/*---------------------------------------------------------------------------*/

/** @brief The buffers this port asks for: few, as SRAM is shared. */
static void espw_config(wifi_init_config_t *c) {
    memset(c, 0, sizeof *c);
    c->osi_funcs = &espw_osi_funcs;
    c->wpa_crypto_funcs.size = sizeof c->wpa_crypto_funcs;
    c->wpa_crypto_funcs.version = ESP_WIFI_CRYPTO_VERSION;
    c->static_rx_buf_num = 4;
    c->dynamic_rx_buf_num = 8;
    c->tx_buf_type = 1;                 /* dynamic */
    c->dynamic_tx_buf_num = 8;
    c->rx_mgmt_buf_num = 5;
    c->nano_enable = 1;                 /* newlib's nano printf */
    c->rx_ba_win = 6;
    c->beacon_max_len = 752;
    c->mgmt_sbuf_num = 12;
    c->espnow_max_encrypt_num = 7;
    c->tx_hetb_queue_num = 1;
    c->wifi_task_stack_size = 3072;
    c->magic = (int)WIFI_INIT_CONFIG_MAGIC;
}

/**
 * @brief Undo whatever of the bring-up happened, in reverse.  The heap goes
 *        back only empty: a block the libraries kept, or a task that did
 *        not end, keeps it taken (and says so) for the next start.
 */
static void espw_teardown(int stage) {
    int ended = 1;

    if (stage >= 3) {
        espw_wpa_unregister();
        (void)esp_wifi_deinit_internal();
        espw_modem_wifi_inited(0);
        espw_modem_wifi_clock_off();
    }
    if (stage >= 2) {
        ended = espw_osi_stop() == 0;
    }
    if (ended && espw_heap_used() == 0U) {
        espw_heap_reset();
        (void)tiku_mem_workspace_close(&espw_heap_arena);
        espw_heap_taken = 0U;
    } else {
        ESPW_PRINTF("heap kept: %lu bytes still in use\n",
                    (unsigned long)espw_heap_used());
    }
}

static int espw_power_up(void) {
    tiku_mem_request_t req = TIKU_MEM_REQUEST_DEFAULT;
    wifi_init_config_t cfg;
    espw_heap_stats_t st;
    esp_err_t rc;
    uint32_t cal_us;
    int fresh;

    if (!espw_heap_taken) {
        req.alignment = 8U;
        req.allocation_class = TIKU_MEM_TRANSIENT;
        if (tiku_mem_workspace_open(&espw_heap_arena,
                                    TIKU_DRV_WIFI_ESP_HEAP_BYTES, &req) !=
            TIKU_MEM_OK) {
            ESPW_PRINTF("no %u KB for the radio's heap in the SRAM tier\n",
                        TIKU_DRV_WIFI_ESP_HEAP_BYTES / 1024U);
            return TIKU_DRV_ERR_INIT;
        }
        espw_heap_init(espw_heap_arena.buf, espw_heap_arena.capacity);
        espw_heap_taken = 1U;
    }
    espw_modem_init();
    if (espw_osi_start() != 0) {
        espw_teardown(2);
        return TIKU_DRV_ERR_INIT;
    }

    espw_config(&cfg);
    rc = esp_wifi_init_internal(&cfg);
    if (rc != ESP_OK) {
        ESPW_PRINTF("init: 0x%lx\n", (unsigned long)rc);
        espw_teardown(2);
        return TIKU_DRV_ERR_INIT;
    }
    espw_modem_wifi_inited(1);
    (void)esp_wifi_internal_set_log_level(2);   /* warnings and errors */
    rc = espw_wpa_register() == 0 ? ESP_OK : ESP_ERR_NO_MEM;
    if (rc == ESP_OK) {
        rc = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (rc == ESP_OK) {
        rc = esp_wifi_start();
    }
    if (rc != ESP_OK) {
        ESPW_PRINTF("start: 0x%lx\n", (unsigned long)rc);
        espw_teardown(3);
        return TIKU_DRV_ERR_INIT;
    }
    (void)esp_wifi_get_mac(WIFI_IF_STA, espw_mac);
    espw_up = 1U;
    tiku_esp32c61_sleep_hold(1);        /* the modem runs on the PLL */

    rc = espw_phy_cal_result(&cal_us, &fresh);
    espw_heap_stats(&st);
    if (fresh) {
        ESPW_PRINTF("RF calibrated: %ld in %lu ms\n", (long)rc,
                    (unsigned long)(cal_us / 1000U));
    }
    ESPW_PRINTF("up: MAC %02x:%02x:%02x:%02x:%02x:%02x\n", espw_mac[0],
                espw_mac[1], espw_mac[2], espw_mac[3], espw_mac[4],
                espw_mac[5]);
    ESPW_PRINTF("heap: %lu of %lu bytes in use, %lu at most\n",
                (unsigned long)(st.size - st.free), (unsigned long)st.size,
                (unsigned long)(st.size - st.low));
    return TIKU_DRV_OK;
}

static int espw_power_down(void) {
    esp_err_t rc = esp_wifi_stop();

    if (rc != ESP_OK) {
        ESPW_PRINTF("stop: 0x%lx\n", (unsigned long)rc);
    }
    espw_up = 0U;
    espw_scanning = 0U;
    tiku_esp32c61_sleep_hold(0);
    espw_teardown(3);
    ESPW_PRINTF("down\n");
    return TIKU_DRV_OK;
}

/*---------------------------------------------------------------------------*/
/* The wireless interface                                                    */
/*---------------------------------------------------------------------------*/

int tiku_wireless_power(uint8_t on) {
    if (!espw_ready) {
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    if ((on != 0U) == (espw_up != 0U)) {
        return TIKU_DRV_OK;
    }
    return on ? espw_power_up() : espw_power_down();
}

int tiku_wireless_status(tiku_wireless_status_t *out) {
    if (out == NULL) {
        return TIKU_DRV_ERR_INVALID;
    }
    memset(out, 0, sizeof *out);
    out->up = espw_up;
    memcpy(out->mac, espw_mac, sizeof out->mac);
    out->irq_count = espw_irq_count();
    out->link_state = TIKU_WIRELESS_LINK_IDLE;
    out->scan_in_progress = espw_scanning;
    out->scan_aps_found = espw_scan_found;
    out->last_scan_ticks = espw_scan_ticks;
    return TIKU_DRV_OK;
}

int tiku_wireless_scan_start(void) {
    if (!espw_up) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (espw_scanning) {
        return TIKU_DRV_ERR_TIMEOUT;
    }
    return tiku_process_post(&espw_runner, TIKU_WIRELESS_EVT_SCAN_START,
                             NULL) ? TIKU_DRV_OK : TIKU_DRV_ERR_TIMEOUT;
}

uint8_t tiku_wireless_scan_results(tiku_wireless_ap_t *out,
                                   uint8_t max_results) {
    uint8_t n = espw_ap_count < max_results ? espw_ap_count : max_results;

    if (out != NULL && n != 0U) {
        memcpy(out, espw_aps, n * sizeof *out);
    }
    return out != NULL ? n : 0U;
}

/* Joining arrives with the supplicant; until then, refused. */

int tiku_wireless_connect_auth(const char *ssid, const char *psk,
                               tiku_wireless_auth_t auth) {
    (void)ssid;
    (void)psk;
    (void)auth;
    return TIKU_DRV_ERR_INVALID;
}

int tiku_wireless_connect(const char *ssid, const char *psk) {
    return tiku_wireless_connect_auth(ssid, psk, TIKU_WIRELESS_AUTH_WPA2_PSK);
}

int tiku_wireless_disconnect(void) {
    return TIKU_DRV_ERR_INVALID;
}

int tiku_wireless_forget(void) {
    return TIKU_DRV_OK;
}

/*---------------------------------------------------------------------------*/
/* The driver                                                                */
/*---------------------------------------------------------------------------*/

static int espw_init(void) {
    (void)espw_roots;
    if (!tiku_drv_wifi_esp_xip_ok()) {
        ESPW_PRINTF("xip.bin in flash is not this build's -- make flash "
                    "writes both images\n");
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    (void)espw_read_mac(espw_mac, ESP_MAC_WIFI_STA);
    ESPW_PRINTF("libraries run from flash: phy %s, net80211 %s, pp %s\n",
                get_phy_version_str(), libnet80211_reversion_git,
                libpp_reversion_git);
    (void)tiku_process_register("wifi-esp", &espw_runner);
    espw_ready = 1U;
    return TIKU_DRV_OK;
}

static int espw_deinit(void) {
    if (espw_up) {
        (void)espw_power_down();
    }
    espw_ready = 0U;
    return TIKU_DRV_OK;
}

const tiku_drv_t tiku_drv_wifi_esp = {
    .name   = "esp-wifi",
    .class  = TIKU_DRV_CLASS_WIFI,
    .init   = espw_init,
    .deinit = espw_deinit,
};
