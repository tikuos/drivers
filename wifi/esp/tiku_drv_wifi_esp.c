/*
 * Tiku Drivers - ESP32-C61 Wi-Fi over Espressif's radio libraries
 *
 * The libraries' code lives in the XIP window, written as xip.bin beside the
 * boot image; a flash holding another build's xip.bin (the arch's header
 * check) is refused, never called into.  The radio is off until asked: up
 * takes a heap from the SRAM tier and starts the OS adapter and the station;
 * down stops them and gives the heap back.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <hal/tiku_cpu.h>
#include <interfaces/wireless/tiku_wireless.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/process/tiku_process.h>
#include <kernel/threads/tiku_thread.h>
#include <kernel/timers/tiku_clock.h>
#include <arch/esp32c61/tiku_sleep_arch.h>
#include <arch/esp32c61/tiku_xip_arch.h>
#include <tikukits/crypto/pbkdf2/tiku_kits_crypto_pbkdf2.h>
#include "tiku_drv_wifi_esp.h"
#include "esp_heap.h"
#include "esp_port.h"

/* The runner's own events, posted from the stack's task: a scan finished,
 * the station joined, the station left (data: the reason), frames came. */
#define ESPW_EV_SCAN_DONE (TIKU_EVENT_USER + 0x20U)
#define ESPW_EV_LINK_UP   (TIKU_EVENT_USER + 0x21U)
#define ESPW_EV_LINK_DOWN (TIKU_EVENT_USER + 0x22U)
#define ESPW_EV_RX        (TIKU_EVENT_USER + 0x23U)

/* Received frames waiting for the kernel thread, at most. */
#define ESPW_RX_SLOTS    8U
#define ESPW_ETH_HDR     14U
#define ESPW_ETH_MAX     1514U

/* Attempts at a join, the first included, before it is reported failed. */
#define ESPW_JOIN_TRIES  3U

/* WPA2's passphrase-to-PMK mapping: PBKDF2-HMAC-SHA1, 4096 rounds. */
#define ESPW_PSK_ROUNDS  4096U
#define ESPW_PMK_LEN     32U

/* The libraries' heap, taken from the SRAM tier while the radio is up: 30 KB
 * at rest, 36 KB at most measured joined with IP traffic and a scan. */
#ifndef TIKU_DRV_WIFI_ESP_HEAP_BYTES
#define TIKU_DRV_WIFI_ESP_HEAP_BYTES (48U * 1024U)
#endif

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

/* A join asked for, until the runner takes it up; the passphrase lasts
 * only until its PMK is derived. */
static char              espw_want_ssid[33];
static char              espw_want_psk[64];
static uint8_t           espw_want;

/* The network joined, or being joined, and how that stands. */
static char              espw_ssid[33];
static volatile uint8_t  espw_link;         /* tiku_wireless_link_t */
static uint8_t           espw_tries;
static uint8_t           espw_bssid[6];
static uint8_t           espw_channel;
static uint32_t          espw_link_raw;     /* the last reason it dropped */
static tiku_clock_time_t espw_join_start;
static uint32_t          espw_join_ticks;

/* Frames received, from the stack's task to the kernel thread: the stack's
 * own buffers, queued by pointer and given back once delivered. */
typedef struct {
    void    *buf;
    void    *eb;
    uint16_t len;
} espw_rx_slot_t;

static espw_rx_slot_t     espw_rxq[ESPW_RX_SLOTS];
static uint8_t            espw_rx_head;     /* the stack's task moves it */
static uint8_t            espw_rx_tail;     /* the kernel thread moves it */
static uint8_t            espw_rx_posted;   /* the runner has an event coming */
static tiku_waitq_t       espw_rx_waitq;    /* a poller waiting for frames */
static tiku_wireless_rx_t espw_rx_cb;
static void              *espw_rx_ctx;

TIKU_PROCESS(espw_runner, "wifi-esp");

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
    case WIFI_EVENT_STA_CONNECTED:
        if (data != NULL) {
            const wifi_event_sta_connected_t *c = data;

            memcpy(espw_bssid, c->bssid, sizeof espw_bssid);
            espw_channel = c->channel;
        }
        (void)tiku_process_post(&espw_runner, ESPW_EV_LINK_UP, NULL);
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        (void)tiku_process_post(&espw_runner, ESPW_EV_LINK_DOWN,
            (tiku_event_data_t)(uintptr_t)(data != NULL ?
                ((const wifi_event_sta_disconnected_t *)data)->reason : 0U));
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

/*---------------------------------------------------------------------------*/
/* Joining                                                                   */
/*---------------------------------------------------------------------------*/

static uint32_t espw_ms(uint32_t ticks) {
    return (uint32_t)(ticks * 1000UL / TIKU_CLOCK_SECOND);
}

/** @brief Why a join or a link ended, for the reasons seen most. */
static const char *espw_reason(uint32_t reason) {
    switch (reason) {
    case 2U:   return "authentication expired";
    case 15U:
    case 204U: return "key handshake timed out -- wrong passphrase?";
    case 200U: return "the AP's beacons stopped";
    case 201U: return "no AP of that name";
    case 202U: return "authentication failed";
    case 203U: return "association failed";
    case 210U:
    case 211U: return "no AP of that name with WPA2-PSK or open";
    default:   return "an IEEE 802.11 reason";
    }
}

/** @brief The join is over without a link (@p what says why): say so, and
 *         to whoever listens. */
static void espw_join_failed(uint32_t why, const char *what) {
    espw_link = TIKU_WIRELESS_LINK_FAILED;
    espw_link_raw = why;
    espw_join_ticks = (uint32_t)(tiku_clock_time_t)(tiku_clock_time() -
                                                    espw_join_start);
    ESPW_PRINTF("join FAILED: %s -- %s (code %lu, %lu ms)\n", espw_ssid,
                what, (unsigned long)why,
                (unsigned long)espw_ms(espw_join_ticks));
    (void)tiku_process_post(TIKU_PROCESS_BROADCAST,
                            TIKU_WIRELESS_EVT_LINK_DOWN,
                            (tiku_event_data_t)(uintptr_t)why);
}

/** @brief Leave the network: no rejoining after.  The stack's word that
 *         the station left comes later, and is not a loss. */
static void espw_leave(void) {
    uint8_t was = espw_link;

    espw_link = TIKU_WIRELESS_LINK_IDLE;
    espw_tries = 0U;
    espw_wpa_set_pmk(NULL);
    if (was != TIKU_WIRELESS_LINK_JOINED &&
        was != TIKU_WIRELESS_LINK_CONNECTING) {
        return;
    }
    (void)esp_wifi_disconnect_internal();
    if (was == TIKU_WIRELESS_LINK_JOINED) {
        ESPW_PRINTF("*** LINK DOWN -- left %s ***\n", espw_ssid);
        (void)tiku_process_post(TIKU_PROCESS_BROADCAST,
                                TIKU_WIRELESS_EVT_LINK_DOWN,
                                (tiku_event_data_t)(uintptr_t)0);
    }
}

/**
 * @brief Configure the station for the network asked for and start joining,
 *        leaving the one joined first.  The passphrase becomes its PMK here
 *        and is wiped; PMF goes off; the strongest AP of the name is tried.
 */
static void espw_join_begin(void) {
    size_t slen, plen = strlen(espw_want_psk);
    uint8_t pmk[ESPW_PMK_LEN];
    wifi_config_t cfg;
    esp_err_t rc;

    if (!espw_want || !espw_up) {
        espw_want = 0U;
        memset(espw_want_psk, 0, sizeof espw_want_psk);
        return;
    }
    espw_want = 0U;
    if (espw_link == TIKU_WIRELESS_LINK_JOINED) {
        espw_leave();
    }
    memcpy(espw_ssid, espw_want_ssid, sizeof espw_ssid);
    slen = strlen(espw_ssid);
    espw_link = TIKU_WIRELESS_LINK_CONNECTING;
    espw_tries = 0U;
    espw_join_start = tiku_clock_time();

    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.sta.ssid, espw_ssid, slen);
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.features = WIFI_STA_NO_WPA3_COMPAT;
    if (plen != 0U) {
        (void)tiku_kits_crypto_pbkdf2_hmac_sha1(
            (const uint8_t *)espw_want_psk, plen, (const uint8_t *)espw_ssid,
            slen, ESPW_PSK_ROUNDS, pmk, sizeof pmk);
        espw_wpa_set_pmk(pmk);
        memset(pmk, 0, sizeof pmk);
        memcpy(cfg.sta.password, espw_want_psk, plen);
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        espw_wpa_set_pmk(NULL);
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    memset(espw_want_psk, 0, sizeof espw_want_psk);
    rc = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    memset(&cfg, 0, sizeof cfg);
    if (rc == ESP_OK) {
        rc = esp_wifi_disable_pmf_config(WIFI_IF_STA);
    }
    if (rc == ESP_OK) {
        rc = esp_wifi_connect_internal();
    }
    if (rc != ESP_OK) {
        espw_join_failed((uint32_t)rc, "the stack refused it");
    }
}

/** @brief The station has joined: the handshake (if any) is done. */
static void espw_join_up(void) {
    if (espw_link != TIKU_WIRELESS_LINK_CONNECTING) {
        return;                 /* left since, or a repeat */
    }
    espw_link = TIKU_WIRELESS_LINK_JOINED;
    espw_tries = 0U;
    espw_join_ticks = (uint32_t)(tiku_clock_time_t)(tiku_clock_time() -
                                                    espw_join_start);
    ESPW_PRINTF("*** LINK UP -- joined %s (channel %u) in %lu ms ***\n",
                espw_ssid, (unsigned)espw_channel,
                (unsigned long)espw_ms(espw_join_ticks));
    (void)tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_WIRELESS_EVT_LINK_UP,
                            (tiku_event_data_t)(uintptr_t)0);
}

/**
 * @brief The station has no link.  Leaving (reason 8) is the station's own
 *        doing and changes nothing; a join that failed is tried again,
 *        ESPW_JOIN_TRIES in all; a link that dropped is rejoined so.
 */
static void espw_join_down(uint32_t reason) {
    if (!espw_up || (reason == WIFI_REASON_ASSOC_LEAVE &&
                     espw_link != TIKU_WIRELESS_LINK_JOINED)) {
        return;
    }
    espw_link_raw = reason;
    if (espw_link == TIKU_WIRELESS_LINK_JOINED) {
        ESPW_PRINTF("*** LINK DOWN -- lost %s: %s (reason %lu) ***\n",
                    espw_ssid, espw_reason(reason), (unsigned long)reason);
        (void)tiku_process_post(TIKU_PROCESS_BROADCAST,
                                TIKU_WIRELESS_EVT_LINK_DOWN,
                                (tiku_event_data_t)(uintptr_t)reason);
        espw_link = TIKU_WIRELESS_LINK_CONNECTING;
        espw_tries = 0U;
        espw_join_start = tiku_clock_time();
    } else if (espw_link != TIKU_WIRELESS_LINK_CONNECTING) {
        return;
    } else if (++espw_tries >= ESPW_JOIN_TRIES) {
        espw_join_failed(reason, espw_reason(reason));
        return;
    } else {
        ESPW_PRINTF("join attempt %u failed: %s (reason %lu); trying again\n",
                    (unsigned)espw_tries, espw_reason(reason),
                    (unsigned long)reason);
    }
    if (esp_wifi_connect_internal() != ESP_OK) {
        espw_join_failed(reason, espw_reason(reason));
    }
}

/*---------------------------------------------------------------------------*/
/* Frames                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief The stack's receiver, on its task: queue the frame for the kernel
 *        thread -- one runner event outstanding at most, and a poller woken.
 *        With no receiver, or the queue full, the frame goes straight back.
 */
static esp_err_t espw_rx(void *buffer, uint16_t len, void *eb) {
    uint8_t next, post = 0U;

    tiku_atomic_enter();
    next = (uint8_t)((espw_rx_head + 1U) % ESPW_RX_SLOTS);
    if (espw_rx_cb != NULL && next != espw_rx_tail) {
        espw_rxq[espw_rx_head].buf = buffer;
        espw_rxq[espw_rx_head].eb = eb;
        espw_rxq[espw_rx_head].len = len;
        espw_rx_head = next;
        post = !espw_rx_posted;
        espw_rx_posted = 1U;
        eb = NULL;
    }
    tiku_atomic_exit();
    if (eb != NULL) {
        esp_wifi_internal_free_rx_buffer(eb);
        return ESP_OK;
    }
    tiku_thread_wake_all(&espw_rx_waitq);
    if (post && !tiku_process_post(&espw_runner, ESPW_EV_RX, NULL)) {
        espw_rx_posted = 0U;            /* the next frame posts again */
    }
    return ESP_OK;
}

/** @brief In the kernel thread: hand each queued frame to the receiver
 *         (unless @p deliver is 0), then give its buffer back.
 *         @return Frames delivered */
static int espw_rx_drain(int deliver) {
    int n = 0;

    for (;;) {
        espw_rx_slot_t f;

        tiku_atomic_enter();
        if (espw_rx_tail == espw_rx_head) {
            tiku_atomic_exit();
            return n;
        }
        f = espw_rxq[espw_rx_tail];
        espw_rx_tail = (uint8_t)((espw_rx_tail + 1U) % ESPW_RX_SLOTS);
        tiku_atomic_exit();
        if (deliver && espw_rx_cb != NULL &&
            espw_link == TIKU_WIRELESS_LINK_JOINED) {
            espw_rx_cb((const uint8_t *)f.buf, f.len, espw_rx_ctx);
            n++;
        }
        esp_wifi_internal_free_rx_buffer(f.eb);
    }
}

/** @brief The runner: starts what the interface asked for, in the kernel
 *         thread, and follows what the stack finished. */
TIKU_PROCESS_THREAD(espw_runner, ev, data)
{
    TIKU_PROCESS_BEGIN();
    for (;;) {
        TIKU_PROCESS_WAIT_EVENT();
        if (ev == TIKU_WIRELESS_EVT_SCAN_START) {
            espw_scan_begin();
        } else if (ev == ESPW_EV_SCAN_DONE && espw_scanning) {
            espw_scan_collect((uint32_t)(uintptr_t)data);
        } else if (ev == TIKU_WIRELESS_EVT_JOIN_START) {
            espw_join_begin();
        } else if (ev == ESPW_EV_LINK_UP) {
            espw_join_up();
        } else if (ev == ESPW_EV_LINK_DOWN) {
            espw_join_down((uint32_t)(uintptr_t)data);
        } else if (ev == TIKU_WIRELESS_EVT_DISCONNECT) {
            espw_leave();
        } else if (ev == ESPW_EV_RX) {
            espw_rx_posted = 0U;        /* before the drain: a frame after it
                                         * posts again */
            (void)espw_rx_drain(1);
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
    espw_crypto_table(&c->wpa_crypto_funcs);
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
    (void)esp_wifi_internal_reg_rxcb(WIFI_IF_STA, espw_rx);
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
    espw_heap_stats_t st;
    esp_err_t rc;

    espw_want = 0U;
    memset(espw_want_psk, 0, sizeof espw_want_psk);
    espw_leave();
    (void)esp_wifi_internal_reg_rxcb(WIFI_IF_STA, NULL);
    rc = esp_wifi_stop();
    if (rc != ESP_OK) {
        ESPW_PRINTF("stop: 0x%lx\n", (unsigned long)rc);
    }
    (void)espw_rx_drain(0);             /* the stack's buffers, back */
    espw_up = 0U;
    espw_scanning = 0U;
    tiku_esp32c61_sleep_hold(0);
    espw_heap_stats(&st);
    espw_teardown(3);
    ESPW_PRINTF("down: the heap peaked at %lu of %lu bytes, %lu refused\n",
                (unsigned long)(st.size - st.low), (unsigned long)st.size,
                (unsigned long)st.fails);
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
    int rssi = 0;

    if (out == NULL) {
        return TIKU_DRV_ERR_INVALID;
    }
    memset(out, 0, sizeof *out);
    out->up = espw_up;
    memcpy(out->mac, espw_mac, sizeof out->mac);
    out->irq_count = espw_irq_count();
    out->scan_in_progress = espw_scanning;
    out->scan_aps_found = espw_scan_found;
    out->last_scan_ticks = espw_scan_ticks;
    out->link_state = espw_link;
    out->link_status_raw = espw_link_raw;
    out->last_join_ticks = espw_join_ticks;
    if (espw_up && espw_link == TIKU_WIRELESS_LINK_JOINED) {
        out->joined_ssid_len = (uint8_t)strlen(espw_ssid);
        memcpy(out->joined_ssid, espw_ssid, out->joined_ssid_len);
        memcpy(out->joined_bssid, espw_bssid, sizeof out->joined_bssid);
        if (esp_wifi_sta_get_rssi(&rssi) == ESP_OK) {
            out->rssi_dbm = (int16_t)rssi;
        }
    }
    return TIKU_DRV_OK;
}

int tiku_wireless_scan_start(void) {
    if (!espw_up) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (espw_scanning || espw_want ||
        espw_link == TIKU_WIRELESS_LINK_CONNECTING) {
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

/* WPA2-PSK, or an open network for an empty passphrase; WPA3 not yet. */
int tiku_wireless_connect_auth(const char *ssid, const char *psk,
                               tiku_wireless_auth_t auth) {
    size_t slen, plen;

    if (!espw_up || ssid == NULL || psk == NULL ||
        auth != TIKU_WIRELESS_AUTH_WPA2_PSK) {
        return TIKU_DRV_ERR_INVALID;
    }
    slen = strnlen(ssid, sizeof espw_want_ssid);
    plen = strnlen(psk, sizeof espw_want_psk);
    if (slen == 0U || slen > 32U || (plen != 0U && (plen < 8U || plen > 63U))) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (espw_want || espw_scanning ||
        espw_link == TIKU_WIRELESS_LINK_CONNECTING) {
        return TIKU_DRV_ERR_TIMEOUT;
    }
    memcpy(espw_want_ssid, ssid, slen + 1U);
    memcpy(espw_want_psk, psk, plen + 1U);
    espw_want = 1U;
    if (!tiku_process_post(&espw_runner, TIKU_WIRELESS_EVT_JOIN_START, NULL)) {
        espw_want = 0U;
        memset(espw_want_psk, 0, sizeof espw_want_psk);
        return TIKU_DRV_ERR_TIMEOUT;
    }
    return TIKU_DRV_OK;
}

int tiku_wireless_connect(const char *ssid, const char *psk) {
    return tiku_wireless_connect_auth(ssid, psk, TIKU_WIRELESS_AUTH_WPA2_PSK);
}

int tiku_wireless_disconnect(void) {
    if (!espw_up) {
        return TIKU_DRV_ERR_INVALID;
    }
    espw_want = 0U;                     /* a join not yet begun is dropped */
    memset(espw_want_psk, 0, sizeof espw_want_psk);
    return tiku_process_post(&espw_runner, TIKU_WIRELESS_EVT_DISCONNECT,
                             NULL) ? TIKU_DRV_OK : TIKU_DRV_ERR_TIMEOUT;
}

/* Nothing is kept across boots: forgetting is leaving. */
int tiku_wireless_forget(void) {
    if (espw_up) {
        (void)tiku_wireless_disconnect();
    }
    return TIKU_DRV_OK;
}

int tiku_wireless_tx_eth(const uint8_t *frame, uint16_t len) {
    int rc;

    if (frame == NULL || len < ESPW_ETH_HDR || len > ESPW_ETH_MAX ||
        !espw_up || espw_link != TIKU_WIRELESS_LINK_JOINED) {
        return TIKU_DRV_ERR_INVALID;
    }
    rc = esp_wifi_internal_tx(WIFI_IF_STA, (void *)(uintptr_t)frame, len);
    if (rc == ESP_OK) {
        return TIKU_DRV_OK;
    }
    return rc == ESP_ERR_NO_MEM ? TIKU_DRV_ERR_TIMEOUT : TIKU_DRV_ERR_INVALID;
}

int tiku_wireless_set_rx(tiku_wireless_rx_t cb, void *ctx) {
    if (!espw_ready) {
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    tiku_atomic_enter();
    espw_rx_cb = cb;
    espw_rx_ctx = ctx;
    tiku_atomic_exit();
    return TIKU_DRV_OK;
}

/* The stack's task is a worker: the kernel thread, busy, must give it the CPU
 * -- up to a tick, less when a frame comes -- before frames can come. */
int tiku_wireless_rx_poll(void) {
    if (!espw_up || !tiku_thread_in_kernel()) {
        return 0;
    }
    tiku_atomic_enter();
    if (espw_rx_tail == espw_rx_head) {
        (void)tiku_thread_wait(&espw_rx_waitq, 1UL);
    }
    tiku_atomic_exit();
    return espw_rx_drain(1);
}

/*---------------------------------------------------------------------------*/
/* The driver                                                                */
/*---------------------------------------------------------------------------*/

static int espw_init(void) {
    (void)espw_roots;
    if (!tiku_esp32c61_xip_ok()) {
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
