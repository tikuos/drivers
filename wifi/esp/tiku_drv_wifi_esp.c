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
#include <kernel/process/tiku_process.h>
#include <kernel/threads/tiku_thread.h>
#include <kernel/timers/tiku_clock.h>
#include "esp_arch.h"
#if TIKU_C6_RADIO_SERVICE_ENABLE
#include <services/ipc/tiku_ipc_radio.h>
#include <interfaces/wireless/tiku_wireless_ipc.h>
#define RADIO_OWNER_CHECK() do { if(!tiku_radio_local_allowed())return TIKU_DRV_ERR_TIMEOUT; } while(0)
#else
#define RADIO_OWNER_CHECK() do { } while(0)
#endif
#include <tikukits/crypto/pbkdf2/tiku_kits_crypto_pbkdf2.h>
#include "tiku_drv_wifi_esp.h"
#include "esp_heap.h"
#include "esp_port.h"
#if TIKU_ESP_SAE_ENABLE
#include "esp_sae.h"
#endif
#if TIKU_WIRELESS_AP_ENABLE
#include "esp_ap.h"
#include <interfaces/wireless/tiku_wireless_ap.h>
#include <kernel/timers/tiku_timer.h>
static tiku_wireless_ap_status_t espw_ap;
static uint8_t espw_ap_mode, espw_dual, espw_ap_mac[6];
static tiku_wireless_rx_if_t espw_rx_if_cb;
static struct tiku_timer espw_ap_timer;
static void espw_ap_configure(wifi_config_t *cfg, const uint8_t *pmk);
#endif

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
 * at rest, 36 KB at most measured joined with IP traffic and a scan; with
 * the receiver built, 32 KB, its packet buffers in PSRAM, so the bank the
 * receiver borrows fits beside it (peak 22 KB measured so).  The C5 scans
 * both bands and keeps a 96-byte record per AP heard: 48 KB ran out at 154
 * APs, with the table lost. */
#ifndef TIKU_DRV_WIFI_ESP_HEAP_BYTES
#if (TIKU_DRV_SDR_ESP_ENABLE + 0)
#define TIKU_DRV_WIFI_ESP_HEAP_BYTES (32U * 1024U)
#elif defined(PLATFORM_ESP32C5)
#define TIKU_DRV_WIFI_ESP_HEAP_BYTES (64U * 1024U)
#else
#define TIKU_DRV_WIFI_ESP_HEAP_BYTES (48U * 1024U)
#endif
#endif

/* A PSRAM block the libraries' packet buffers prefer (their _wifi_malloc
 * family), as IDF's SPIRAM_TRY_ALLOCATE_WIFI_LWIP; 0: all in SRAM.  With
 * BLE or the receiver built too it is on: SRAM must fit them as well. */
#ifndef TIKU_DRV_WIFI_ESP_PSRAM_BYTES
#if ESPW_COEX || (TIKU_DRV_SDR_ESP_ENABLE + 0)
#define TIKU_DRV_WIFI_ESP_PSRAM_BYTES (96U * 1024U)
#else
#define TIKU_DRV_WIFI_ESP_PSRAM_BYTES 0U
#endif
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
static uint8_t      espw_mac[6];

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
static tiku_wireless_auth_t espw_want_auth;
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
    uint8_t interface;
} espw_rx_slot_t;

static espw_rx_slot_t     espw_rxq[ESPW_RX_SLOTS];
static uint8_t            espw_rx_head;     /* the stack's task moves it */
static uint8_t            espw_rx_tail;     /* the kernel thread moves it */
static uint8_t            espw_rx_posted;   /* the runner has an event coming */
static tiku_waitq_t       espw_rx_waitq;    /* a poller waiting for frames */
static tiku_wireless_rx_t espw_rx_cb;
static void              *espw_rx_ctx;

TIKU_PROCESS(espw_runner, "wifi-esp");

#define ESPW_CREDENTIAL_WORKER (TIKU_C6_RADIO_SERVICE_ENABLE || TIKU_ESP_SAE_ENABLE)
#if ESPW_CREDENTIAL_WORKER
/** @brief Erase credential buffers through volatile stores. */
static void espw_key_wipe(void *data, size_t size)
{
    volatile uint8_t *p = data;
    while (size--) *p++ = 0;
}
/* The worker owns these inputs until DONE, including after cancellation. */
static struct {
    wifi_config_t config;
    uint8_t pmk[ESPW_PMK_LEN];
    uint8_t active;
    uint8_t cancelled;
    uint8_t access_point;
#if TIKU_ESP_SAE_ENABLE
    void *pt;
    uint32_t *stack;
#endif
} espw_pmk_job;
TIKU_THREAD(espw_pmk_worker, 2048);
#endif

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
#if TIKU_WIRELESS_AP_ENABLE
    case 14: /* AP station connected. */
        espw_ap.clients = espw_ap_clients();
        break;
    case 15: /* AP station disconnected. */
        espw_ap.clients = espw_ap_clients();
        break;
#endif
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

/** @brief Scan permitted channels; C5 listens passively, C61 uses active probes. */
static void espw_scan_begin(void) {
    esp_err_t rc;

    if (!espw_up || espw_scanning) {
        return;
    }
    espw_scanning = 1U;
    espw_scan_start = tiku_clock_time();
#if defined(PLATFORM_ESP32C5)
    const wifi_scan_config_t scan = {.show_hidden = true, .scan_type = 1,
                                     .scan_time = {.passive = 360}};
    rc = esp_wifi_scan_start(&scan, false);
#else
    rc = esp_wifi_scan_start(NULL, false);
#endif
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

#if ESPW_CREDENTIAL_WORKER
    if (!espw_pmk_job.access_point) espw_pmk_job.cancelled = 1U;
#endif

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

/** @brief Apply a station configuration after credential derivation finishes. */
static void espw_join_configure(wifi_config_t *cfg) {
    esp_err_t rc = esp_wifi_set_config(WIFI_IF_STA, cfg);
    if (rc == ESP_OK && !cfg->sta.pmf_cfg.required) {
        rc = esp_wifi_disable_pmf_config(WIFI_IF_STA);
    }
    memset(cfg, 0, sizeof *cfg);
    if (rc == ESP_OK) {
        rc = esp_wifi_connect_internal();
    }
    if (rc != ESP_OK) {
        espw_join_failed((uint32_t)rc, "the stack refused it");
    }
}

#if ESPW_CREDENTIAL_WORKER
/** @brief Derive SAE elements and vendor PSKs on a preemptible credential worker. */
static void espw_pmk_derive(void *arg) {
    (void)arg;
#if TIKU_ESP_SAE_ENABLE
    if (espw_pmk_job.stack) {
        espw_pmk_job.pt = espw_sae_pt_new(espw_pmk_job.config.sta.ssid,
            strnlen((const char *)espw_pmk_job.config.sta.ssid, 32),
            espw_pmk_job.config.sta.password,
            strlen((const char *)espw_pmk_job.config.sta.password));
        if (!espw_pmk_job.pt || !espw_pmk_job.access_point) return;
    }
#endif
    (void)tiku_kits_crypto_pbkdf2_hmac_sha1(
        espw_pmk_job.config.sta.password,
        strlen((const char *)espw_pmk_job.config.sta.password),
        espw_pmk_job.config.sta.ssid,
        strnlen((const char *)espw_pmk_job.config.sta.ssid, 32U),
        ESPW_PSK_ROUNDS, espw_pmk_job.pmk, sizeof espw_pmk_job.pmk);
}

/** @brief Reserve a crypto worker stack and start the credential derivation. */
static int key_worker_start(void)
{
#if TIKU_ESP_SAE_ENABLE
    bool sae = espw_pmk_job.access_point ?
        espw_pmk_job.config.ap.authmode == WIFI_AUTH_WPA3_PSK :
        espw_pmk_job.config.sta.threshold.authmode == WIFI_AUTH_WPA3_PSK;
    if (sae) {
        espw_pmk_job.stack = espw_malloc(4096);
        if (!espw_pmk_job.stack) return -1;
        espw_pmk_worker.stack_base = espw_pmk_job.stack;
        espw_pmk_worker.stack_size = 4096;
    }
#endif
    int rc = tiku_thread_start(&espw_pmk_worker, espw_pmk_derive, NULL);
#if TIKU_ESP_SAE_ENABLE
    if (rc) {
        espw_free(espw_pmk_job.stack); espw_pmk_job.stack = NULL;
        espw_pmk_worker.stack_base = espw_pmk_worker_stack;
        espw_pmk_worker.stack_size = sizeof espw_pmk_worker_stack;
    }
#endif
    return rc;
}

/** @brief Reap a completed derivation; cancelled work cannot join. */
static void espw_pmk_collect(void) {
    if (!espw_pmk_job.active || !tiku_thread_is_done(&espw_pmk_worker)) {
        return;
    }
    (void)tiku_thread_forget(&espw_pmk_worker);
#if TIKU_ESP_SAE_ENABLE
    if (espw_pmk_job.stack && !espw_pmk_job.pt && !espw_pmk_job.cancelled) {
        espw_pmk_job.cancelled = 1;
#if TIKU_WIRELESS_AP_ENABLE
        if (espw_pmk_job.access_point && espw_dual) {
            espw_ap_mode = 0; espw_ap.active = 0;
        } else
#endif
        espw_join_failed((uint32_t)ESP_ERR_NO_MEM, "SAE derivation failed");
    }
#endif
    if (!espw_pmk_job.cancelled && espw_up &&
        (espw_link == TIKU_WIRELESS_LINK_CONNECTING
#if TIKU_WIRELESS_AP_ENABLE
         || (espw_pmk_job.access_point && espw_ap_mode)
#endif
        )) {
#if TIKU_ESP_SAE_ENABLE
        espw_sae_configure(espw_pmk_job.pt, espw_pmk_job.access_point);
        espw_pmk_job.pt = NULL;
#endif
#if TIKU_WIRELESS_AP_ENABLE
        if (espw_pmk_job.access_point)
            espw_ap_configure(&espw_pmk_job.config, espw_pmk_job.pmk);
        else
#endif
        {
            espw_wpa_set_pmk(espw_pmk_job.pmk);
            espw_join_configure(&espw_pmk_job.config);
        }
    }
#if TIKU_ESP_SAE_ENABLE
    espw_sae_pt_free(espw_pmk_job.pt);
    if (espw_pmk_job.stack) espw_key_wipe(espw_pmk_job.stack, 4096);
    espw_free(espw_pmk_job.stack);
    espw_pmk_worker.stack_base = espw_pmk_worker_stack;
    espw_pmk_worker.stack_size = sizeof espw_pmk_worker_stack;
#endif
    espw_key_wipe(&espw_pmk_job, sizeof espw_pmk_job);
    espw_key_wipe(espw_pmk_worker_stack, sizeof espw_pmk_worker_stack);
}
#endif

/**
 * @brief Configure the station for the network asked for and start joining,
 *        leaving the one joined first. Credentials are cleared after setup;
 *        SAE requires PMF, and the strongest matching AP is tried.
 */
static void espw_join_begin(void) {
    size_t slen, plen = strlen(espw_want_psk);
#if !ESPW_CREDENTIAL_WORKER
    uint8_t pmk[ESPW_PMK_LEN];
#endif
    wifi_config_t cfg;

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
        memcpy(cfg.sta.password, espw_want_psk, plen);
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
#if TIKU_ESP_SAE_ENABLE
        if (espw_want_auth == TIKU_WIRELESS_AUTH_WPA3_SAE) {
            cfg.sta.threshold.authmode = WIFI_AUTH_WPA3_PSK;
            cfg.sta.pmf_cfg.capable = cfg.sta.pmf_cfg.required = true;
            cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_HASH_TO_ELEMENT;
            cfg.sta.sae_pk_mode = WPA3_SAE_PK_MODE_DISABLED;
        }
#endif
#if ESPW_CREDENTIAL_WORKER
        espw_pmk_job.config = cfg;
        espw_pmk_job.active = 1U;
        espw_pmk_job.cancelled = 0U;
        espw_pmk_job.access_point = 0U;
        espw_key_wipe(espw_want_psk, sizeof espw_want_psk);
        espw_key_wipe(&cfg, sizeof cfg);
        if (key_worker_start() != 0) {
            espw_key_wipe(&espw_pmk_job, sizeof espw_pmk_job);
            espw_join_failed((uint32_t)ESP_ERR_NO_MEM, "no key worker slot");
        } else if (tiku_thread_await(&espw_pmk_worker, &espw_runner)) {
            tiku_process_poll(&espw_runner);
        }
        return;
#else
        (void)tiku_kits_crypto_pbkdf2_hmac_sha1(
            (const uint8_t *)espw_want_psk, plen, (const uint8_t *)espw_ssid,
            slen, ESPW_PSK_ROUNDS, pmk, sizeof pmk);
        espw_wpa_set_pmk(pmk);
        memset(pmk, 0, sizeof pmk);
#endif
    } else {
        espw_wpa_set_pmk(NULL);
#if TIKU_ESP_SAE_ENABLE
        espw_sae_configure(NULL, false);
#endif
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    memset(espw_want_psk, 0, sizeof espw_want_psk);
    espw_join_configure(&cfg);
}

/** @brief The station has joined: the handshake (if any) is done. */
static void espw_join_up(void) {
#if TIKU_WIRELESS_AP_ENABLE
    if (espw_ap_mode && !espw_dual) return;
#endif
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
    if (
#if TIKU_WIRELESS_AP_ENABLE
        (espw_ap_mode && !espw_dual) ||
#endif
        !espw_up || (reason == WIFI_REASON_ASSOC_LEAVE &&
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
static esp_err_t espw_rx_enqueue(uint8_t interface, void *buffer, uint16_t len, void *eb) {
    uint8_t next, post = 0U;

    tiku_atomic_enter();
    next = (uint8_t)((espw_rx_head + 1U) % ESPW_RX_SLOTS);
    if ((espw_rx_cb != NULL
#if TIKU_WIRELESS_AP_ENABLE
         || espw_rx_if_cb != NULL
#endif
        ) && next != espw_rx_tail) {
        espw_rxq[espw_rx_head].buf = buffer;
        espw_rxq[espw_rx_head].eb = eb;
        espw_rxq[espw_rx_head].len = len;
        espw_rxq[espw_rx_head].interface = interface;
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

/** @brief Retain the station interface identity through deferred delivery. */
static esp_err_t espw_rx(void *buffer, uint16_t len, void *eb)
{ return espw_rx_enqueue(0, buffer, len, eb); }
#if TIKU_WIRELESS_AP_ENABLE
/** @brief Retain the AP interface identity through deferred delivery. */
static esp_err_t espw_rx_ap(void *buffer, uint16_t len, void *eb)
{ return espw_rx_enqueue(1, buffer, len, eb); }
#endif

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
        if (deliver) {
#if TIKU_WIRELESS_AP_ENABLE
            uint8_t active = f.interface == 1 ? espw_ap.active :
                             espw_link == TIKU_WIRELESS_LINK_JOINED;
            if (active && espw_rx_if_cb) {
                espw_rx_if_cb(f.interface, f.buf, f.len, espw_rx_ctx); n++;
            } else if (active && espw_rx_cb && (!espw_dual || !f.interface)) {
                espw_rx_cb(f.buf, f.len, espw_rx_ctx); n++;
            }
#else
            if (espw_rx_cb && espw_link == TIKU_WIRELESS_LINK_JOINED) {
                espw_rx_cb(f.buf, f.len, espw_rx_ctx); n++;
            }
#endif
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
#if TIKU_WIRELESS_AP_ENABLE
        if (espw_ap.active) {
            espw_ap_poll();
            tiku_timer_set_event(&espw_ap_timer, TIKU_CLOCK_SECOND / 4);
        }
#endif
#if ESPW_CREDENTIAL_WORKER
        espw_pmk_collect();
#endif
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
#if TIKU_ESP_SAE_ENABLE
    c->feature_caps = 1u;
    c->wifi_task_stack_size = 6144;
#else
    c->wifi_task_stack_size = 3072;
#endif
    c->magic = (int)WIFI_INIT_CONFIG_MAGIC;
}

/**
 * @brief Undo the bring-up from @p stage on, in reverse: 3 the stack was
 *        initialised, 2 only the core is up.  The core gives the heap back
 *        only empty: a block the libraries kept, or a task that did not
 *        end, keeps it taken (and says so) for the next start.
 */
static void espw_teardown(int stage) {
    if (stage >= 3) {
        espw_wpa_unregister();
#if TIKU_ESP_SAE_ENABLE
        espw_sae_stop();
#endif
        (void)esp_wifi_deinit_internal();
        espw_modem_wifi_inited(0);
        espw_modem_wifi_clock_off();
    }
    espw_core_down(ESPW_RADIO_WIFI);
}

/** @brief The PSRAM region's use, when the core gave the radio one. */
static void espw_report_ext(const char *what) {
    espw_heap_stats_t x;

    espw_heap_ext_stats(&x);
    if (x.size != 0U) {
        ESPW_PRINTF("%s %lu of %lu bytes in use, %lu at most\n", what,
                    (unsigned long)(x.size - x.free), (unsigned long)x.size,
                    (unsigned long)(x.size - x.low));
    }
}

static int espw_power_up(void) {
    wifi_init_config_t cfg;
    espw_heap_stats_t st;
    esp_err_t rc;
    uint32_t cal_us;
    int fresh;

    if (espw_core_up(ESPW_RADIO_WIFI, TIKU_DRV_WIFI_ESP_HEAP_BYTES,
                     TIKU_DRV_WIFI_ESP_PSRAM_BYTES) != 0) {
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
#if (ESPW_TRACE + 0)
    (void)esp_wifi_internal_set_log_level(4);
#else
    (void)esp_wifi_internal_set_log_level(2);   /* warnings and errors */
#endif
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
    espw_arch_sleep_hold(1);        /* the modem runs on the PLL */

    rc = espw_phy_cal_result(&cal_us, &fresh);
    espw_heap_stats(&st);
    if (fresh) {
        ESPW_PRINTF("RF calibrated: %ld in %lu ms\n", (long)rc,
                    (unsigned long)(cal_us / 1000U));
    }
    ESPW_PRINTF("up: MAC %02x:%02x:%02x:%02x:%02x:%02x\n", espw_mac[0],
                espw_mac[1], espw_mac[2], espw_mac[3], espw_mac[4],
                espw_mac[5]);
    espw_report_ext("PSRAM heap:");
    ESPW_PRINTF("heap: %lu of %lu bytes in use, %lu at most\n",
                (unsigned long)(st.size - st.free), (unsigned long)st.size,
                (unsigned long)(st.size - st.low));
    return TIKU_DRV_OK;
}

static int espw_power_down(void) {
    espw_heap_stats_t st;
    esp_err_t rc;

    espw_want = 0U;
#if ESPW_CREDENTIAL_WORKER
    espw_pmk_job.cancelled = 1;
#if TIKU_ESP_SAE_ENABLE
    if (espw_pmk_job.active && espw_pmk_job.stack) {
        (void)tiku_thread_join(&espw_pmk_worker);
        espw_pmk_collect();
    }
#endif
#endif
    memset(espw_want_psk, 0, sizeof espw_want_psk);
#if TIKU_WIRELESS_AP_ENABLE
    if (espw_ap_mode) {
        espw_link = TIKU_WIRELESS_LINK_IDLE;
        (void)esp_wifi_internal_reg_rxcb(WIFI_IF_AP, NULL);
        espw_ap = (tiku_wireless_ap_status_t){0};
        espw_ap_mode = 0;
        (void)tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_WIRELESS_EVT_LINK_DOWN, NULL);
    } else
#endif
        espw_leave();
    (void)esp_wifi_internal_reg_rxcb(WIFI_IF_STA, NULL);
    rc = esp_wifi_stop();
    if (rc != ESP_OK) {
        ESPW_PRINTF("stop: 0x%lx\n", (unsigned long)rc);
    }
    (void)espw_rx_drain(0);             /* the stack's buffers, back */
    espw_up = 0U;
#if TIKU_WIRELESS_AP_ENABLE
    espw_dual = 0;
#endif
    espw_scanning = 0U;
    espw_arch_sleep_hold(0);
    espw_heap_stats(&st);
    espw_report_ext("down: PSRAM heap");
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
    RADIO_OWNER_CHECK();
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
        if (
#if TIKU_WIRELESS_AP_ENABLE
            (!espw_ap_mode || espw_dual) &&
#endif
            esp_wifi_sta_get_rssi(&rssi) == ESP_OK) {
            out->rssi_dbm = (int16_t)rssi;
        }
    }
    return TIKU_DRV_OK;
}

int tiku_wireless_scan_start(void) {
    RADIO_OWNER_CHECK();
    if (!espw_up
#if TIKU_WIRELESS_AP_ENABLE
        || (espw_ap_mode && !espw_dual)
#endif
    ) {
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

/* Empty WPA2 credentials select open authentication; SAE requires a password. */
int tiku_wireless_connect_auth(const char *ssid, const char *psk,
                               tiku_wireless_auth_t auth) {
    RADIO_OWNER_CHECK();
    size_t slen, plen;

    if (!espw_up || ssid == NULL || psk == NULL ||
#if TIKU_WIRELESS_AP_ENABLE
        (espw_ap_mode && !espw_dual) ||
#endif
        (auth != TIKU_WIRELESS_AUTH_WPA2_PSK
#if TIKU_ESP_SAE_ENABLE
         && auth != TIKU_WIRELESS_AUTH_WPA3_SAE
#endif
        )) {
        return TIKU_DRV_ERR_INVALID;
    }
    slen = strnlen(ssid, sizeof espw_want_ssid);
    plen = strnlen(psk, sizeof espw_want_psk);
    if (slen == 0U || slen > 32U || plen > 63U ||
        (auth == TIKU_WIRELESS_AUTH_WPA3_SAE ? !plen : (plen && plen < 8U))) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (
#if ESPW_CREDENTIAL_WORKER
        espw_pmk_job.active ||
#endif
        espw_want || espw_scanning ||
        espw_link == TIKU_WIRELESS_LINK_CONNECTING) {
        return TIKU_DRV_ERR_TIMEOUT;
    }
    memcpy(espw_want_ssid, ssid, slen + 1U);
    memcpy(espw_want_psk, psk, plen + 1U);
    espw_want_auth = auth;
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
    RADIO_OWNER_CHECK();
    if (!espw_up
#if TIKU_WIRELESS_AP_ENABLE
        || (espw_ap_mode && !espw_dual)
#endif
    ) {
        return TIKU_DRV_ERR_INVALID;
    }
#if ESPW_CREDENTIAL_WORKER
    if (!espw_pmk_job.access_point) espw_pmk_job.cancelled = 1U;
#endif
    espw_want = 0U;                     /* a join not yet begun is dropped */
    memset(espw_want_psk, 0, sizeof espw_want_psk);
    return tiku_process_post(&espw_runner, TIKU_WIRELESS_EVT_DISCONNECT,
                             NULL) ? TIKU_DRV_OK : TIKU_DRV_ERR_TIMEOUT;
}

/* Nothing is kept across boots: forgetting is leaving. */
int tiku_wireless_forget(void) {
    RADIO_OWNER_CHECK();
#if TIKU_WIRELESS_AP_ENABLE
    if (espw_ap_mode) {
        int rc = tiku_wireless_ap_stop();
        if (rc || !espw_dual) return rc;
    }
#endif
    if (espw_up) {
        (void)tiku_wireless_disconnect();
    }
    return TIKU_DRV_OK;
}

/* No profile is saved, so there is none to describe. */
int tiku_wireless_saved_profile(tiku_wireless_saved_profile_t *out) {
    if (out == NULL) {
        return TIKU_DRV_ERR_INVALID;
    }
    memset(out, 0, sizeof *out);
    return TIKU_DRV_ERR_NOT_PRESENT;
}

int tiku_wireless_tx_eth(const uint8_t *frame, uint16_t len) {
    RADIO_OWNER_CHECK();
    int rc;

    if (frame == NULL || len < ESPW_ETH_HDR || len > ESPW_ETH_MAX ||
        !espw_up || espw_link != TIKU_WIRELESS_LINK_JOINED) {
        return TIKU_DRV_ERR_INVALID;
    }
    rc = esp_wifi_internal_tx(
#if TIKU_WIRELESS_AP_ENABLE
        espw_ap_mode && !espw_dual ? WIFI_IF_AP :
#endif
        WIFI_IF_STA, (void *)(uintptr_t)frame, len);
    if (rc == ESP_OK) {
        return TIKU_DRV_OK;
    }
    return rc == ESP_ERR_NO_MEM ? TIKU_DRV_ERR_TIMEOUT : TIKU_DRV_ERR_INVALID;
}

int tiku_wireless_set_rx(tiku_wireless_rx_t cb, void *ctx) {
    RADIO_OWNER_CHECK();
    if (!espw_ready) {
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    tiku_atomic_enter();
    espw_rx_cb = cb;
#if TIKU_WIRELESS_AP_ENABLE
    espw_rx_if_cb = NULL;
#endif
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
    if (!espw_arch_xip_ok()) {
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

#if TIKU_WIRELESS_AP_ENABLE
/** @brief Apply AP configuration and restore the concurrent station receiver. */
static void espw_ap_configure(wifi_config_t *cfg, const uint8_t *pmk)
{
    int rc;
    /* A hexadecimal PMK keeps configuration from deriving it on the Wi-Fi task. */
    if (pmk) {
        static const char hex[] = "0123456789abcdef";
        for (unsigned i = 0; i < 32; i++) {
            cfg->ap.password[2 * i] = hex[pmk[i] >> 4];
            cfg->ap.password[2 * i + 1] = hex[pmk[i] & 15];
        }
    }
    (void)esp_wifi_internal_reg_rxcb(WIFI_IF_STA, NULL);
    rc = esp_wifi_stop();
    (void)espw_rx_drain(0);
    uint8_t reconnect = espw_dual && espw_link == TIKU_WIRELESS_LINK_JOINED;
    if (!rc) rc = esp_wifi_set_mode(espw_dual ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (!rc) rc = esp_wifi_set_config(WIFI_IF_AP, cfg);
    if (!rc && !cfg->ap.pmf_cfg.required) rc = esp_wifi_disable_pmf_config(WIFI_IF_AP);
    espw_ap_key(pmk);
    if (!rc) rc = esp_wifi_start();
    if (!rc) rc = esp_wifi_internal_reg_rxcb(WIFI_IF_AP, espw_rx_ap);
    if (!rc) rc = esp_wifi_get_mac(WIFI_IF_AP, espw_ap_mac);
    if (!rc && espw_dual) rc = esp_wifi_internal_reg_rxcb(WIFI_IF_STA, espw_rx);
    if (rc) {
        espw_ap.active = 0;
        espw_link = TIKU_WIRELESS_LINK_FAILED;
        espw_link_raw = (uint32_t)rc;
        if (reconnect)
            (void)tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_WIRELESS_EVT_LINK_DOWN, NULL);
    } else {
        espw_ap.active = 1;
        espw_ap.channel = cfg->ap.channel;
        espw_ap.secured = cfg->ap.authmode == WIFI_AUTH_WPA3_PSK ? 2 :
                          cfg->ap.authmode != WIFI_AUTH_OPEN;
        if (!espw_dual) {
            memcpy(espw_mac, espw_ap_mac, 6); memcpy(espw_bssid, espw_ap_mac, 6);
            espw_link = TIKU_WIRELESS_LINK_JOINED;
            (void)tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_WIRELESS_EVT_LINK_UP, NULL);
        } else if (reconnect) {
            espw_link = TIKU_WIRELESS_LINK_CONNECTING;
            (void)esp_wifi_connect_internal();
        }
        tiku_process_poll(&espw_runner);
    }
    memset(cfg, 0, sizeof *cfg);
}
int tiku_wireless_ap_start_auth(const char *ssid, const char *psk, uint8_t channel,
                                tiku_wireless_auth_t auth)
{
    RADIO_OWNER_CHECK();
    if (!espw_up || !ssid || !psk || channel < 1 || channel > 11)
        return TIKU_DRV_ERR_INVALID;
    size_t sl = strnlen(ssid, 33), pl = strnlen(psk, 64);
    if (!sl || sl > 32 || pl > 63 ||
        (auth == TIKU_WIRELESS_AUTH_WPA3_SAE ? !pl : (pl && pl < 8)) ||
        (auth != TIKU_WIRELESS_AUTH_WPA2_PSK
#if TIKU_ESP_SAE_ENABLE
         && auth != TIKU_WIRELESS_AUTH_WPA3_SAE
#endif
        )) return TIKU_DRV_ERR_INVALID;
    if (espw_ap_mode || espw_want || espw_scanning ||
        (espw_link != TIKU_WIRELESS_LINK_IDLE && !(espw_dual && espw_link == TIKU_WIRELESS_LINK_JOINED))
#if ESPW_CREDENTIAL_WORKER
        || espw_pmk_job.active
#endif
    ) return TIKU_DRV_ERR_TIMEOUT;
    wifi_config_t cfg = {0};
    memcpy(cfg.ap.ssid, ssid, sl);
    memcpy(cfg.ap.password, psk, pl);
    cfg.ap.ssid_len = sl;
    cfg.ap.channel = channel;
    cfg.ap.authmode = pl ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
#if TIKU_ESP_SAE_ENABLE
    if (auth == TIKU_WIRELESS_AUTH_WPA3_SAE) {
        cfg.ap.authmode = WIFI_AUTH_WPA3_PSK;
        cfg.ap.pmf_cfg.capable = cfg.ap.pmf_cfg.required = true;
        cfg.ap.sae_pwe_h2e = WPA3_SAE_PWE_HASH_TO_ELEMENT;
    }
#endif
    cfg.ap.max_connection = ESPW_AP_MAX_CLIENTS;
    cfg.ap.beacon_interval = 100;
    cfg.ap.dtim_period = 1;
    cfg.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    if (!espw_dual) memcpy(espw_ssid, ssid, sl + 1);
    espw_ap_mode = 1;
    if (!espw_dual) espw_link = TIKU_WIRELESS_LINK_CONNECTING;
#if ESPW_CREDENTIAL_WORKER
    if (pl) {
        espw_pmk_job.config = cfg;
        espw_pmk_job.active = espw_pmk_job.access_point = 1;
        espw_pmk_job.cancelled = 0;
        memset(&cfg, 0, sizeof cfg);
        if (key_worker_start()) {
            espw_key_wipe(&espw_pmk_job, sizeof espw_pmk_job);
            espw_ap_mode = 0;
            if (!espw_dual) espw_link = TIKU_WIRELESS_LINK_IDLE;
            return TIKU_DRV_ERR_TIMEOUT;
        }
        (void)tiku_thread_await(&espw_pmk_worker, &espw_runner);
    } else {
#if TIKU_ESP_SAE_ENABLE
        espw_sae_configure(NULL, true);
#endif
        espw_ap_configure(&cfg, NULL);
    }
#else
    uint8_t pmk[32];
    if (pl) tiku_kits_crypto_pbkdf2_hmac_sha1((const uint8_t *)psk, pl,
              (const uint8_t *)ssid, sl, ESPW_PSK_ROUNDS, pmk, sizeof pmk);
    espw_ap_configure(&cfg, pl ? pmk : NULL);
    memset(pmk, 0, sizeof pmk);
#endif
    return TIKU_DRV_OK;
}
int tiku_wireless_ap_start(const char *ssid, const char *psk, uint8_t channel)
{
    return tiku_wireless_ap_start_auth(ssid, psk, channel, TIKU_WIRELESS_AUTH_WPA2_PSK);
}
int tiku_wireless_ap_stop(void)
{
    RADIO_OWNER_CHECK();
    if (!espw_ap_mode) return TIKU_DRV_OK;
    if (espw_dual) {
#if ESPW_CREDENTIAL_WORKER
        if (espw_pmk_job.active && espw_pmk_job.access_point) espw_pmk_job.cancelled = 1;
#endif
        int rc = esp_wifi_set_mode(WIFI_MODE_STA);
        if (rc) return TIKU_DRV_ERR_IO;
        (void)esp_wifi_internal_reg_rxcb(WIFI_IF_AP, NULL);
        espw_ap_mode = 0; espw_ap = (tiku_wireless_ap_status_t){0};
#if TIKU_ESP_SAE_ENABLE
        espw_sae_configure(NULL, true);
#endif
        return TIKU_DRV_OK;
    }
    int r = espw_power_down();
    return r ? r : espw_power_up();
}
int tiku_wireless_ap_status(tiku_wireless_ap_status_t *out)
{
    if (!out) return TIKU_DRV_ERR_INVALID;
    *out = espw_ap;
    out->clients = espw_ap.active ? espw_ap_clients() : 0;
    if (espw_dual && espw_link == TIKU_WIRELESS_LINK_JOINED) out->channel = espw_channel;
    return TIKU_DRV_OK;
}
#endif

#if TIKU_WIRELESS_AP_ENABLE
int tiku_wireless_dual(uint8_t enable)
{
    RADIO_OWNER_CHECK();
    if (!espw_up || enable > 1) return TIKU_DRV_ERR_INVALID;
    if (espw_ap_mode || espw_want || espw_scanning || espw_link != TIKU_WIRELESS_LINK_IDLE)
        return TIKU_DRV_ERR_TIMEOUT;
    espw_dual = enable;
    return TIKU_DRV_OK;
}
uint8_t tiku_wireless_is_dual(void) { return espw_dual; }
int tiku_wireless_ap_mac(uint8_t mac[6])
{
    if (!mac || !espw_up) return TIKU_DRV_ERR_INVALID;
    return esp_wifi_get_mac(WIFI_IF_AP, mac) ? TIKU_DRV_ERR_IO : TIKU_DRV_OK;
}
int tiku_wireless_set_rx_if(tiku_wireless_rx_if_t cb, void *ctx)
{
    RADIO_OWNER_CHECK();
    if (!espw_ready) return TIKU_DRV_ERR_NOT_PRESENT;
    tiku_atomic_enter();
    espw_rx_if_cb = cb; espw_rx_cb = NULL; espw_rx_ctx = ctx;
    tiku_atomic_exit();
    return TIKU_DRV_OK;
}
int tiku_wireless_tx_if(uint8_t interface, const uint8_t *frame, uint16_t len)
{
    RADIO_OWNER_CHECK();
    if (interface > 1 || !frame || len < ESPW_ETH_HDR || len > ESPW_ETH_MAX || !espw_up ||
        (interface ? !espw_ap.active : espw_link != TIKU_WIRELESS_LINK_JOINED))
        return TIKU_DRV_ERR_INVALID;
    int rc = esp_wifi_internal_tx(interface ? WIFI_IF_AP : WIFI_IF_STA, (void *)frame, len);
    return !rc ? TIKU_DRV_OK : rc == ESP_ERR_NO_MEM ? TIKU_DRV_ERR_TIMEOUT : TIKU_DRV_ERR_IO;
}
#endif
