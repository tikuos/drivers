/*
 * Tiku Drivers - ESP32-C61 BLE: the controller's life cycle and its HCI
 *
 * Espressif's LE controller library under tikuOS's own host stack.  On:
 * register what the controller calls (ext, NPL, coexistence), clock the BLE
 * MAC, initialise and enable the controller in the order IDF's
 * esp_bt_controller_init/enable use (read as reference, never copied), and
 * give the host a tiku_bt_transport_t over the controller's in-memory HCI.
 * Off undoes all of it and gives every byte back.
 *
 * The controller's code runs from flash (IDF's run-in-flash-only mode, with
 * its relaxed timing); its task is a worker thread; its interrupt lines are
 * held across flash writes like Wi-Fi's.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tiku.h"
#include <hal/tiku_cpu.h>
#include <kernel/threads/tiku_thread.h>
#include <arch/esp32c61/tiku_cpu_freq_boot_arch.h>
#include <arch/esp32c61/tiku_irq_arch.h>
#include <arch/esp32c61/tiku_sleep_arch.h>
#include <arch/esp32c61/tiku_trng_arch.h>
#include <arch/esp32c61/tiku_xip_arch.h>
#include <interfaces/bluetooth/tiku_bt.h>
#include <interfaces/bluetooth/tiku_bt_transport.h>
#include <tikukits/crypto/p256/tiku_kits_crypto_p256.h>

#include "esp_ble.h"
#include "esp_ble_abi.h"
#include "esp_coex_abi.h"
#include "esp_heap.h"
#include "esp_port.h"
#include "tiku_drv_ble_esp.h"

/* The controller's heap, from the SRAM tier while it is on: 28 KB in use at
 * rest, 33 KB at most measured scanning and advertising at once. */
#ifndef TIKU_DRV_BLE_ESP_HEAP_BYTES
#define TIKU_DRV_BLE_ESP_HEAP_BYTES (40U * 1024U)
#endif

/* HCI packets on their way to the host: type byte, then the packet.  The
 * host drains it every tick; a busy room's reports fit in 2 KB. */
#define ESPB_RX_BYTES       2048U

/* Bring-up trace: HCI commands, events and receives. */
#ifndef ESPB_TRACE
#define ESPB_TRACE 0
#endif

/* The library's interrupt lines, one per source, apart from Wi-Fi's (1). */
#define ESPB_LIB_LINE(src)  (16U + (uint32_t)(src))

/* IDF's scan-duplicate defaults: filter on address and PDU type (mesh's
 * packets excepted), 20 devices remembered. */
#define DUP_ADDRESS_PDU     0x05U
#define DUP_MESH_EXCEPTION  0x10U
#define DUP_ALL             0xFFFFFFFFUL
#define DUP_CACHE           20U

/* The msys buffer pools, from the heap: IDF's sizes, a third of its 12 + 24
 * blocks -- one link's ACL at a 23-byte ATT MTU needs few. */
#define MSYS_1_SIZE         256U
#define MSYS_2_SIZE         320U
#define MSYS_1_COUNT        8U
#define MSYS_2_COUNT        8U

static uint8_t       espb_ready;        /* xip.bin is this build's */
static uint8_t       espb_up;           /* enabled: HCI flows */
static tiku_thread_t *espb_task;        /* the controller's task */
static tiku_waitq_t  espb_idle_wq;      /* woken as that task blocks */
static espb_config_t espb_cfg;
static uint32_t      espb_heap_peak;

/*---------------------------------------------------------------------------*/
/* What the controller calls                                                 */
/*---------------------------------------------------------------------------*/

static int espb_intr_alloc(int source, int flags, void (*handler)(void *),
                           void *arg, void **ret_handle) {
    uint32_t n = ESPB_LIB_LINE(source);

    (void)flags;
    espw_osi_funcs._set_intr(0, (uint32_t)source, n, 1);
    espw_osi_funcs._set_isr((int32_t)n, (void *)handler, arg);
    espw_osi_funcs._ints_on(1UL << n);
    if (ret_handle != NULL) {
        *ret_handle = (void *)(uintptr_t)(n + 1U);
    }
    return 0;
}

static int espb_intr_free(void **ret_handle) {
    uint32_t n;

    if (ret_handle == NULL || *ret_handle == NULL) {
        return 0;
    }
    n = (uint32_t)(uintptr_t)*ret_handle - 1U;
    espw_osi_funcs._ints_off(1UL << n);
    espw_osi_funcs._set_isr((int32_t)n, NULL, NULL);
    *ret_handle = NULL;
    return 0;
}

/* The controller treats 1 as made, -1 as not (FreeRTOS's codes). */
static int espb_task_create(void *fn, const char *name, uint32_t depth,
                            void *param, uint32_t prio, void *handle,
                            uint32_t core) {
    if (espw_osi_funcs._task_create_pinned_to_core(fn, name, depth, param,
                                                   prio, handle, core) != 1) {
        return -1;
    }
    /* A task's handle begins with its thread (esp_osi.c's task record). */
    espb_task = (handle != NULL) ? *(tiku_thread_t **)handle : NULL;
    return 1;
}

static void espb_task_delete(void *handle) {
    if ((void *)espb_task == handle) {
        espb_task = NULL;
    }
    espw_osi_funcs._task_delete(handle);
}

static void espb_assert(const uint32_t ln, const char *fn, uint32_t p1,
                        uint32_t p2) {
    ESPB_PRINTF("controller assert: line %lu in %s (0x%lx, 0x%lx)\n",
                (unsigned long)ln, fn != NULL ? fn : "?", (unsigned long)p1,
                (unsigned long)p2);
    __builtin_trap();
}

static uint32_t espb_random(void) {
    uint32_t v = 0U;

    (void)tiku_trng_arch_read_u32(&v);
    return v;
}

/* LE Secure Connections' P-256, for the controller's Read Local P-256
 * Public Key and Generate DHKey: TikuKits' ECDH, the values little-endian
 * as the controller keeps them.  A failure is SMP's key error (0x17). */
#define ESPB_KEY_ERR    0x17

/* The Core spec's debug private key (Vol 3 Part H 2.3.5.6.1), big-endian:
 * a pair that happens to be it is drawn again. */
static const uint8_t espb_debug_priv[32] = {
    0x3f, 0x49, 0xf6, 0xd4, 0xa3, 0xc5, 0x5f, 0x38, 0x74, 0xc9, 0xb3, 0xe3,
    0xd2, 0x10, 0x3f, 0x50, 0x4a, 0xff, 0x60, 0x7b, 0xeb, 0x40, 0xb7, 0x99,
    0x58, 0x99, 0xb8, 0xa6, 0xcd, 0x3c, 0x1a, 0xbd
};

static void swap32(uint8_t *dst, const uint8_t *src) {
    for (unsigned i = 0U; i < 32U; i++) {
        dst[i] = src[31U - i];
    }
}

static int espb_ecc_key_pair(uint8_t *pub, uint8_t *priv) {
    uint8_t seed[32], d[32], q[TIKU_KITS_CRYPTO_P256_PUB_LEN];
    int rc;

    do {
        if (tiku_trng_arch_read_bytes(seed, sizeof seed) != 0) {
            return ESPB_KEY_ERR;
        }
        rc = tiku_kits_crypto_p256_ecdh_keypair(seed, d, q);
    } while (rc != TIKU_KITS_CRYPTO_P256_OK ||
             memcmp(d, espb_debug_priv, sizeof d) == 0);
    swap32(pub, q + 1);
    swap32(pub + 32, q + 33);
    swap32(priv, d);
    memset(seed, 0, sizeof seed);
    memset(d, 0, sizeof d);
    return 0;
}

static int espb_ecc_dh_key(const uint8_t *x, const uint8_t *y,
                           const uint8_t *priv, uint8_t *dhkey) {
    uint8_t peer[TIKU_KITS_CRYPTO_P256_PUB_LEN], d[32], dh[32];
    int rc;

    peer[0] = 0x04U;
    swap32(peer + 1, x);
    swap32(peer + 33, y);
    swap32(d, priv);
    rc = tiku_kits_crypto_p256_ecdh_shared(d, peer, dh);
    memset(d, 0, sizeof d);
    if (rc != TIKU_KITS_CRYPTO_P256_OK) {
        return ESPB_KEY_ERR;            /* not a point on the curve */
    }
    swap32(dhkey, dh);
    memset(dh, 0, sizeof dh);
    return 0;
}

static espb_ext_funcs_t espb_ext = {
    .ext_version = ESPB_EXT_VERSION,
    ._esp_intr_alloc = espb_intr_alloc,
    ._esp_intr_free = espb_intr_free,
    ._malloc = espw_malloc,
    ._free = espw_free,
    ._task_create = espb_task_create,
    ._task_delete = espb_task_delete,
    ._osi_assert = espb_assert,
    ._os_random = espb_random,
    ._ecc_gen_key_pair = espb_ecc_key_pair,
    ._ecc_gen_dh_key = espb_ecc_dh_key,
    .magic = ESPB_EXT_MAGIC,
};

/* The arbiter's scheme bits: forwarded with Wi-Fi built too, else idle. */
static void espb_coex_set(uint32_t type, uint32_t status) {
#if ESPW_COEX
    coex_schm_status_bit_set(type, status);
#else
    (void)type;
    (void)status;
#endif
}

static void espb_coex_clear(uint32_t type, uint32_t status) {
#if ESPW_COEX
    coex_schm_status_bit_clear(type, status);
#else
    (void)type;
    (void)status;
#endif
}

static espb_coex_funcs_t espb_coex = {
    ._magic = ESPB_COEX_MAGIC,
    ._version = ESPB_COEX_VERSION,
    ._coex_wifi_sleep_set = NULL,
    ._coex_core_ble_conn_dyn_prio_get = NULL,
    ._coex_schm_status_bit_set = espb_coex_set,
    ._coex_schm_status_bit_clear = espb_coex_clear,
};

/** @brief IDF's default controller configuration for the C61 (controller
 *         only, HCI in memory, BLE 5 features on), cut to what tikuOS's host
 *         uses: one link, a 23-byte ATT MTU, legacy advertising. */
static void espb_config(espb_config_t *c) {
    tiku_esp32c61_clock_t clk;

    tiku_cpu_esp32c61_clock_probe(&clk);
    memset(c, 0, sizeof *c);
    c->config_version = ESPB_CONFIG_VERSION;
    c->ble_ll_resolv_list_size = 4U;
    c->ble_hci_evt_hi_buf_count = 8U;   /* IDF: 30 */
    c->ble_hci_evt_lo_buf_count = 8U;
    c->ble_ll_sync_list_cnt = 5U;
    c->ble_ll_sync_cnt = 1U;
    c->ble_ll_rsp_dup_list_count = 20U;
    c->ble_ll_adv_dup_list_count = 20U;
    c->ble_ll_tx_pwr_dbm = 9U;
    c->rtc_freq = espw_modem_bt_lp_hz();
    c->ble_ll_sca = 60U;
    c->ble_ll_scan_phy_number = 2U;
    c->ble_ll_conn_def_auth_pyld_tmo = 3000U;
    c->ble_ll_jitter_usecs = 16U;
    c->ble_ll_sched_max_adv_pdu_usecs = 376U;
    c->ble_ll_sched_direct_adv_max_usecs = 502U;
    c->ble_ll_sched_adv_max_usecs = 852U;
    c->ble_scan_rsp_data_max_len = 251U;    /* IDF: 1650 */
    c->ble_ll_cfg_num_hci_cmd_pkts = 1U;
    c->ble_ll_ctrl_proc_timeout_ms = 40000U;
    c->nimble_max_connections = 1U;     /* the host keeps one */
    c->ble_whitelist_size = 12U;
    c->ble_acl_buf_size = 255U;         /* IDF: 517 x 10 */
    c->ble_acl_buf_count = 4U;
    c->ble_hci_evt_buf_size = 257U;
    c->ble_multi_adv_instances = 1U;
    c->ble_ext_adv_max_size = 251U;     /* IDF: 1650 */
    c->controller_task_stack_size = 4096U;
    c->controller_task_prio = 23U;
    c->cca_rssi_thresh = (uint8_t)(256U - 50U);
    c->ble_scan_classify_filter_enable = 1U;
    c->main_xtal_freq = 40U;
    c->cpu_freq_mhz = (uint8_t)(clk.cpu_hz / 1000000UL);
    c->csa2_select = 1U;
    c->scan_backoff_upperlimitmax = 32U;
    c->ble_data_lenth_zero_aux = 1U;
    c->vhci_enabled = 1U;
    c->fast_conn_data_tx_en = 1U;
    c->ch39_txpwr = 9;
    c->adv_rsv_cnt = 1U;
    c->conn_rsv_cnt = 1U;
    c->priority_level_cfg = (1U << 4) | (1U << 2);  /* sync, periodic: mid */
    c->config_magic = ESPB_CONFIG_MAGIC;
}

/*---------------------------------------------------------------------------*/
/* The controller's feature modules (IDF's ble.c)                            */
/*---------------------------------------------------------------------------*/

static int espb_stack_init_env(void) {
    int rc = base_stack_initEnv();

    rc = rc ? rc : adv_stack_initEnv();
    rc = rc ? rc : extAdv_stack_initEnv();
    rc = rc ? rc : scan_stack_initEnv();
    rc = rc ? rc : sync_stack_initEnv();
    rc = rc ? rc : conn_stack_initEnv();
    return rc;
}

static void espb_stack_deinit_env(void) {
    conn_stack_deinitEnv();
    sync_stack_deinitEnv();
    scan_stack_deinitEnv();
    extAdv_stack_deinitEnv();
    adv_stack_deinitEnv();
    base_stack_deinitEnv();
}

static int espb_stack_enable(void) {
    int rc = base_stack_enable();

    rc = rc ? rc : adv_stack_enable();
    rc = rc ? rc : extAdv_stack_enable();
    rc = rc ? rc : scan_stack_enable();
    rc = rc ? rc : sync_stack_enable();
    rc = rc ? rc : conn_stack_enable();
    return rc;
}

static void espb_stack_disable(void) {
    conn_stack_disable();
    sync_stack_disable();
    scan_stack_disable();
    extAdv_stack_disable();
    adv_stack_disable();
    base_stack_disable();
}

/*---------------------------------------------------------------------------*/
/* HCI: the controller's packets, queued for the host                        */
/*---------------------------------------------------------------------------*/

/* A byte ring of records: length (2 bytes, little-endian), then the packet
 * with its type byte, so recv() hands the host what an HCI UART would. */
static uint8_t     *rx_ring;
static uint32_t     rx_head, rx_used;
static uint32_t     rx_dropped;
static tiku_waitq_t rx_wq;

static void ring_put(const uint8_t *src, uint32_t n) {
    uint32_t at = (rx_head + rx_used) % ESPB_RX_BYTES;

    for (uint32_t i = 0U; i < n; i++) {
        rx_ring[at] = src[i];
        at = (at + 1U) % ESPB_RX_BYTES;
    }
    rx_used += n;
}

static void ring_take(uint8_t *dst, uint32_t n) {
    for (uint32_t i = 0U; i < n; i++) {
        if (dst != NULL) {
            dst[i] = rx_ring[rx_head];
        }
        rx_head = (rx_head + 1U) % ESPB_RX_BYTES;
    }
    rx_used -= n;
}

/** @brief Queue one packet: @p type, then @p n bytes of @p body or, when
 *         @p om is set, of that buffer chain. */
static void rx_queue(uint8_t type, const uint8_t *body,
                     const struct os_mbuf *om, uint32_t n) {
    uint8_t hdr[3] = { (uint8_t)(n + 1U), (uint8_t)((n + 1U) >> 8), type };

    tiku_atomic_enter();
    if (rx_ring == NULL || !espb_up || rx_used + 3U + n > ESPB_RX_BYTES) {
        rx_dropped++;
    } else {
        ring_put(hdr, 3U);
        if (om != NULL) {
            uint8_t chunk[32];

            for (uint32_t off = 0U; off < n; off += sizeof chunk) {
                uint32_t k = n - off < sizeof chunk ? n - off : sizeof chunk;

                (void)r_os_mbuf_copydata(om, (int)off, (int)k, chunk);
                ring_put(chunk, k);
            }
        } else {
            ring_put(body, n);
        }
        tiku_thread_wake_all(&rx_wq);
    }
    tiku_atomic_exit();
}

static int espb_evt_cb(uint8_t *hci_ev, void *arg) {
    (void)arg;
#if ESPB_TRACE
    ESPB_PRINTF("evt %02x len %u: %02x %02x %02x %02x\n", hci_ev[0], hci_ev[1],
                hci_ev[2], hci_ev[3], hci_ev[4], hci_ev[5]);
#endif
    rx_queue(0x04U, hci_ev, NULL, (uint32_t)hci_ev[1] + 2U);
    r_ble_hci_trans_buf_free(hci_ev);
    return 0;
}

static int espb_acl_cb(struct os_mbuf *om, void *arg) {
    const struct os_mbuf_pkthdr *ph =
        (const struct os_mbuf_pkthdr *)(const void *)(om + 1);

    (void)arg;
    rx_queue(0x02U, NULL, om, ph->omp_len);
    (void)r_os_mbuf_free_chain(om);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* The host's transport                                                      */
/*---------------------------------------------------------------------------*/

void espb_controller_idle(void) {
    tiku_thread_wake_all(&espb_idle_wq);
}

/**
 * @brief Let the controller's task finish what it is doing: it frees a
 *        command's buffer only after the reply that woke the host, and a
 *        FreeRTOS task of its priority would finish before the host ran.
 *        Kernel thread only; a few ticks at most.
 */
static void espb_settle(void) {
    if (espb_task == NULL || !tiku_thread_in_kernel()) {
        return;
    }
    tiku_atomic_enter();
    for (unsigned n = 0U; n < 16U && espb_task != NULL &&
                          tiku_thread_state(espb_task) != TIKU_THREAD_BLOCKED;
         n++) {
        (void)tiku_thread_wait(&espb_idle_wq, 1UL);
    }
    tiku_atomic_exit();
}

static int espb_send(const uint8_t *pkt, uint16_t len) {
    if (!espb_up) {
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    espb_settle();
    if (pkt == NULL || len < 2U) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (pkt[0] == 0x01U) {                          /* command */
        uint8_t *cmd;

        if (len < 4U || len != 4U + pkt[3]) {
            return TIKU_DRV_ERR_INVALID;
        }
        cmd = r_ble_hci_trans_buf_alloc(ESPB_HCI_BUF_CMD);
        if (cmd == NULL) {
#if ESPB_TRACE
            ESPB_PRINTF("cmd %02x%02x: no buffer\n", pkt[2], pkt[1]);
#endif
            return TIKU_DRV_ERR_TIMEOUT;
        }
        memcpy(cmd, pkt + 1, (size_t)len - 1U);
#if ESPB_TRACE
        ESPB_PRINTF("cmd %02x%02x len %u\n", pkt[2], pkt[1], pkt[3]);
#endif
        return r_ble_hci_trans_hs_cmd_tx(cmd) == 0 ? TIKU_DRV_OK
                                                    : TIKU_DRV_ERR_INIT;
    }
    if (pkt[0] == 0x02U) {                          /* ACL data */
        struct os_mbuf *om = r_os_msys_get_pkthdr((uint16_t)(len - 1U),
                                                  ESPB_HCI_ACL_LEADING);

        if (om == NULL) {
            return TIKU_DRV_ERR_TIMEOUT;
        }
        if (r_os_mbuf_append(om, pkt + 1, (uint16_t)(len - 1U)) != 0) {
            (void)r_os_mbuf_free_chain(om);
            return TIKU_DRV_ERR_TIMEOUT;
        }
        return r_ble_hci_trans_hs_acl_tx(om) == 0 ? TIKU_DRV_OK
                                                   : TIKU_DRV_ERR_INIT;
    }
    return TIKU_DRV_ERR_INVALID;
}

static int espb_recv(uint8_t *out, uint16_t out_max) {
    uint8_t lb[2];
    uint32_t n;
    int rc;

    if (out == NULL || out_max < 2U) {
        return TIKU_DRV_ERR_INVALID;
    }
    tiku_atomic_enter();
    if (rx_ring == NULL || rx_used == 0U) {
        rc = 0;
    } else {
        lb[0] = rx_ring[rx_head];
        lb[1] = rx_ring[(rx_head + 1U) % ESPB_RX_BYTES];
        n = (uint32_t)lb[0] | ((uint32_t)lb[1] << 8);
        if (n > out_max) {
            rc = TIKU_DRV_ERR_INVALID;      /* left for a larger buffer */
        } else {
            ring_take(NULL, 2U);
            ring_take(out, n);
            rc = (int)n;
        }
#if ESPB_TRACE
        if (rc != 0) {
            ESPB_PRINTF("recv %d (max %u)\n", rc, out_max);
        }
#endif
    }
    tiku_atomic_exit();
    return rc;
}

/** @brief The host waits for a reply: the controller's task gets the CPU
 *         until a packet comes or @p ms (at least a tick) go by. */
static void espb_wait(uint16_t ms) {
    unsigned long ticks = ((unsigned long)ms * TIKU_CLOCK_SECOND + 999UL) /
                          1000UL;

    if (!tiku_thread_in_kernel()) {
        espw_delay_ms(ms);
        return;
    }
    tiku_atomic_enter();
    if (rx_used == 0U) {
        (void)tiku_thread_wait(&rx_wq, ticks != 0UL ? ticks : 1UL);
    }
    tiku_atomic_exit();
}

/**
 * @brief Let the controller's task start: it registers its side of HCI
 *        when it first runs and says so with an HCI NOP.  A FreeRTOS task
 *        of its priority would have run before enable returned; the kernel
 *        thread here must yield to it.  @return 1 once the NOP is in
 */
static int espb_wait_controller(uint32_t ms) {
    espw_deadline_t d;
    int ok;

    espw_deadline_start(&d, ms);
    tiku_atomic_enter();
    while (rx_used == 0U && espw_deadline_wait(&rx_wq, &d, ms)) {
    }
    ok = rx_used != 0U;
    tiku_atomic_exit();
    return ok;
}

static int espb_is_ready(void) {
    return espb_up;
}

static const char *espb_version(void) {
    return ble_controller_get_compile_version();
}

static const tiku_bt_transport_t espb_transport = {
    .send     = espb_send,
    .recv     = espb_recv,
    .is_ready = espb_is_ready,
    .wait     = espb_wait,
    .version  = espb_version,
};

/*---------------------------------------------------------------------------*/
/* On and off                                                                */
/*---------------------------------------------------------------------------*/

/* How far a bring-up got, so a failure (or off) undoes exactly that. */
enum {
    STAGE_NONE, STAGE_CORE, STAGE_TABLES, STAGE_MODEM, STAGE_ENV,
    STAGE_MSYS, STAGE_ENABLED
};

static void espb_note_heap(void) {
    espw_heap_stats_t st;

    espw_heap_stats(&st);
    if (st.size - st.low > espb_heap_peak) {
        espb_heap_peak = st.size - st.low;
    }
}

static void espb_teardown(int stage) {
    if (stage >= STAGE_ENABLED) {
        espb_up = 0U;
        (void)r_ble_controller_disable();
        espb_stack_disable();
#if ESPW_COEX
        coex_disable();
#endif
        espw_phy_bt_disable();
        tiku_esp32c61_sleep_hold(0);
    }
    if (stage >= STAGE_MSYS) {
        r_esp_ble_msys_deinit();        /* HCI callbacks stay: they drop */
    }
    if (stage >= STAGE_MODEM) {
        espw_modem_bt_off();
    }
    if (stage >= STAGE_ENV) {
        espb_stack_deinit_env();
        (void)r_ble_controller_deinit();
    }
    if (stage >= STAGE_MODEM) {
        esp_ble_unregister_bb_funcs();
    }
    if (stage >= STAGE_TABLES) {
        esp_unregister_npl_funcs();
        esp_unregister_ext_funcs();
        espb_npl_deinit();
    }
    espb_note_heap();
    espw_free(rx_ring);
    rx_ring = NULL;
    if (stage >= STAGE_CORE) {
        espw_core_down(ESPW_RADIO_BLE);
    }
}

static int espb_fail(int stage, const char *what, int rc) {
    ESPB_PRINTF("%s: %d\n", what, rc);
    espb_teardown(stage);
    return TIKU_DRV_ERR_INIT;
}

static int espb_power_up(void) {
    espb_npl_counts_t n;
    uint8_t mac[6], le[6];
    int rc;

    if (espw_core_up(ESPW_RADIO_BLE, TIKU_DRV_BLE_ESP_HEAP_BYTES, 0U) != 0) {
        return TIKU_DRV_ERR_INIT;
    }
    rx_ring = espw_malloc(ESPB_RX_BYTES);
    rx_head = rx_used = 0U;
    if (rx_ring == NULL) {
        return espb_fail(STAGE_CORE, "no memory for the HCI ring", -1);
    }

    /* What the controller calls, then the objects it says it will make. */
    rc = esp_register_ext_funcs(&espb_ext);
    if (rc != 0) {
        return espb_fail(STAGE_CORE, "ext funcs", rc);
    }
    rc = esp_register_npl_funcs(&espb_npl_funcs);
    if (rc != 0) {
        esp_unregister_ext_funcs();
        return espb_fail(STAGE_CORE, "npl funcs", rc);
    }
    espw_modem_bt_on();                 /* before the config: its LP clock */
    espb_config(&espb_cfg);
    memset(&n, 0, sizeof n);
    (void)r_ble_get_npl_element_info(&espb_cfg, &n);
    if (espb_npl_init(&n) != 0) {
        return espb_fail(STAGE_MODEM, "no memory for NPL objects", -1);
    }
    rc = ble_osi_coex_funcs_register(&espb_coex);
    if (rc != 0) {
        return espb_fail(STAGE_MODEM, "coex funcs", rc);
    }
#if ESPW_COEX
    (void)coex_init();
#endif
    rc = esp_ble_register_bb_funcs();
    if (rc != 0) {
        return espb_fail(STAGE_MODEM, "baseband funcs", rc);
    }

    rc = r_ble_controller_init(&espb_cfg);
    if (rc != 0) {
        return espb_fail(STAGE_MODEM, "controller init", rc);
    }
    rc = espb_stack_init_env();
    if (rc != 0) {
        return espb_fail(STAGE_ENV, "controller modules", rc);
    }
    r_filter_duplicate_mode_disable(DUP_ALL);
    r_filter_duplicate_mode_enable(DUP_ADDRESS_PDU | DUP_MESH_EXCEPTION);
    r_filter_duplicate_set_ring_list_max_num(DUP_CACHE);
    r_scan_duplicate_cache_refresh_set_time(0U);
    rc = r_esp_ble_msys_init(MSYS_1_SIZE, MSYS_2_SIZE, MSYS_1_COUNT,
                             MSYS_2_COUNT, 1U);
    if (rc != 0) {
        return espb_fail(STAGE_MSYS, "msys", rc);
    }

    /* The public address goes in least significant byte first. */
    (void)espw_read_mac(mac, ESP_MAC_BT);
    for (unsigned i = 0U; i < 6U; i++) {
        le[i] = mac[5U - i];
    }
    (void)r_esp_ble_ll_set_public_addr(le);
    r_ble_hci_trans_cfg_hs(espb_evt_cb, NULL, espb_acl_cb, NULL);
    if (!esp_ble_controller_lib_check()) {
        ESPB_PRINTF("the BLE MAC's hardware date is not the library's\n");
    }

    /* Enable: the PHY for BLE, the baseband, the flash-only timing. */
    espw_phy_bt_enable();
    tiku_esp32c61_sleep_hold(1);
    bt_bb_v2_init_cmplx(1U);
#if ESPW_COEX
    (void)coex_enable();
#endif
    esp_ble_controller_flash_only_param_config();
    rc = espb_stack_enable();
    if (rc == 0) {
        rc = r_ble_controller_enable(ESPB_MODE_BLE);
    }
    espb_up = 1U;
    if (rc != 0) {
        return espb_fail(STAGE_ENABLED, "controller enable", rc);
    }
    if (!espb_wait_controller(500U)) {
        return espb_fail(STAGE_ENABLED, "controller task silent", -1);
    }
    espb_note_heap();
    ESPB_PRINTF("up: address %02x:%02x:%02x:%02x:%02x:%02x, NPL %u/%u/%u/"
                "%u/%u\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                n.evt_count, n.evtq_count, n.co_count, n.sem_count,
                n.mutex_count);
    (void)tiku_bt_register_transport(&espb_transport);
    rc = tiku_bt_init();
    espb_note_heap();
    if (rc != TIKU_DRV_OK) {
        ESPB_PRINTF("host stack: %d\n", rc);
    }
    return TIKU_DRV_OK;
}

static int espb_power_down(void) {
    espw_heap_stats_t st;

    tiku_bt_shutdown();
    espw_heap_stats(&st);
    espb_teardown(STAGE_ENABLED);
    ESPB_PRINTF("down: the heap peaked at %lu of %lu bytes, %lu refused, "
                "%lu packets dropped\n", (unsigned long)espb_heap_peak,
                (unsigned long)st.size, (unsigned long)st.fails,
                (unsigned long)rx_dropped);
    return TIKU_DRV_OK;
}

int tiku_drv_ble_esp_power(uint8_t on) {
    if (!espb_ready) {
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    if ((on != 0U) == (espb_up != 0U)) {
        return TIKU_DRV_OK;
    }
    return on ? espb_power_up() : espb_power_down();
}

/* The host stack's switch (tiku_bt_power(), `bt on/off`). */
int tiku_bt_controller_power(uint8_t on) {
    return tiku_drv_ble_esp_power(on);
}

void tiku_drv_ble_esp_status(tiku_drv_ble_esp_status_t *out) {
    espw_heap_stats_t st;

    memset(out, 0, sizeof *out);
    out->up = espb_up;
    out->version = espb_ready ? ble_controller_get_compile_version() : "";
    if (espb_up) {
        espw_heap_stats(&st);
        out->heap_size = st.size;
        out->heap_used = st.size - st.free;
        out->heap_refused = st.fails;
        espb_note_heap();
    }
    out->heap_peak = espb_heap_peak;
    out->rx_dropped = rx_dropped;
    out->irqs = espw_irq_count();
}

/*---------------------------------------------------------------------------*/
/* The driver                                                                */
/*---------------------------------------------------------------------------*/

static int espb_init(void) {
    if (!tiku_esp32c61_xip_ok()) {
        ESPB_PRINTF("xip.bin in flash is not this build's -- make flash "
                    "writes both images\n");
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    ESPB_PRINTF("controller library %s runs from flash\n",
                ble_controller_get_compile_version());
    espb_ready = 1U;
    return TIKU_DRV_OK;
}

static int espb_deinit(void) {
    if (espb_up) {
        (void)espb_power_down();
    }
    espb_ready = 0U;
    return TIKU_DRV_OK;
}

const tiku_drv_t tiku_drv_ble_esp = {
    .name   = "esp-ble",
    .class  = TIKU_DRV_CLASS_BLE,
    .init   = espb_init,
    .deinit = espb_deinit,
};
