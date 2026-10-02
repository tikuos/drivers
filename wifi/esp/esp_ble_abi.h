/*
 * Tiku Drivers - ESP32-C61 BLE controller library, the ABI this driver uses
 *
 * Hand-written from ESP-IDF 4d59230, the tree whose esp32c6-bt-lib commit
 * fetch.sh pins (the C61 shares the C6's controller glue): the tables the
 * controller takes before init, its config, the NPL object shapes, the
 * os_mempool API it imports, and the calls the driver makes.  The asserts
 * hold the layouts to what the library itself reads (checked in its code:
 * config version at 0 and magic at 112, ext magic at 44).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_ESP_BLE_ABI_H_
#define TIKU_DRV_WIFI_ESP_BLE_ABI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_abi.h"

/* The guard words the library checks. */
#define ESPB_EXT_VERSION        0x20250825UL
#define ESPB_EXT_MAGIC          0xA5A5A5A5UL
#define ESPB_COEX_VERSION       0x00010006UL
#define ESPB_COEX_MAGIC         0xFADEBEADUL
#define ESPB_CONFIG_VERSION     0x20260123UL
#define ESPB_CONFIG_MAGIC       0x5A5AA5A5UL

/* esp_bt_mode_t: LE only on this chip. */
#define ESPB_MODE_BLE           1U

/** @brief Calls the controller makes outside itself (C61: no reset_modem). */
typedef struct {
    uint32_t ext_version;
    int (*_esp_intr_alloc)(int source, int flags, void (*handler)(void *),
                           void *arg, void **ret_handle);
    int (*_esp_intr_free)(void **ret_handle);
    void *(*_malloc)(size_t size);
    void (*_free)(void *p);
    int (*_task_create)(void *task_func, const char *name,
                        uint32_t stack_depth, void *param, uint32_t prio,
                        void *task_handle, uint32_t core_id);
    void (*_task_delete)(void *task_handle);
    void (*_osi_assert)(const uint32_t ln, const char *fn, uint32_t param1,
                        uint32_t param2);
    uint32_t (*_os_random)(void);
    int (*_ecc_gen_key_pair)(uint8_t *pub, uint8_t *priv);
    int (*_ecc_gen_dh_key)(const uint8_t *remote_pub_key_x,
                           const uint8_t *remote_pub_key_y,
                           const uint8_t *local_priv_key, uint8_t *dhkey);
    uint32_t magic;
} espb_ext_funcs_t;

/** @brief The coexistence hooks; a lone BLE radio needs none of them. */
typedef struct {
    uint32_t _magic;
    uint32_t _version;
    void (*_coex_wifi_sleep_set)(bool sleep);
    int (*_coex_core_ble_conn_dyn_prio_get)(bool *low, bool *high);
    void (*_coex_schm_status_bit_set)(uint32_t type, uint32_t status);
    void (*_coex_schm_status_bit_clear)(uint32_t type, uint32_t status);
} espb_coex_funcs_t;

/** @brief The controller's configuration, the C61's layout (IDF's
 *         esp_bt_controller_config_t without the C6's version_num). */
typedef struct {
    uint32_t config_version;
    uint16_t ble_ll_resolv_list_size;
    uint16_t ble_hci_evt_hi_buf_count;
    uint16_t ble_hci_evt_lo_buf_count;
    uint8_t  ble_ll_sync_list_cnt;
    uint8_t  ble_ll_sync_cnt;
    uint16_t ble_ll_rsp_dup_list_count;
    uint16_t ble_ll_adv_dup_list_count;
    uint8_t  ble_ll_tx_pwr_dbm;
    uint64_t rtc_freq;
    uint16_t ble_ll_sca;
    uint8_t  ble_ll_scan_phy_number;
    uint16_t ble_ll_conn_def_auth_pyld_tmo;
    uint8_t  ble_ll_jitter_usecs;
    uint16_t ble_ll_sched_max_adv_pdu_usecs;
    uint16_t ble_ll_sched_direct_adv_max_usecs;
    uint16_t ble_ll_sched_adv_max_usecs;
    uint16_t ble_scan_rsp_data_max_len;
    uint8_t  ble_ll_cfg_num_hci_cmd_pkts;
    uint32_t ble_ll_ctrl_proc_timeout_ms;
    uint16_t nimble_max_connections;
    uint8_t  ble_whitelist_size;
    uint16_t ble_acl_buf_size;
    uint16_t ble_acl_buf_count;
    uint16_t ble_hci_evt_buf_size;
    uint16_t ble_multi_adv_instances;
    uint16_t ble_ext_adv_max_size;
    uint16_t controller_task_stack_size;
    uint8_t  controller_task_prio;
    uint8_t  controller_run_cpu;
    uint8_t  enable_qa_test;
    uint8_t  enable_bqb_test;
    uint8_t  enable_tx_cca;
    uint8_t  cca_rssi_thresh;
    uint8_t  sleep_en;
    uint8_t  coex_phy_coded_tx_rx_time_limit;
    uint8_t  dis_scan_backoff;
    uint8_t  ble_scan_classify_filter_enable;
    uint8_t  cca_drop_mode;
    int8_t   cca_low_tx_pwr;
    uint8_t  main_xtal_freq;
    uint8_t  cpu_freq_mhz;
    uint8_t  ignore_wl_for_direct_adv;
    uint8_t  enable_pcl;
    uint8_t  csa2_select;
    uint8_t  enable_csr;
    uint8_t  ble_aa_check;
    uint8_t  ble_llcp_disc_flag;
    uint16_t scan_backoff_upperlimitmax;
    uint8_t  ble_chan_ass_en;
    uint8_t  ble_data_lenth_zero_aux;
    uint8_t  vhci_enabled;
    uint8_t  ptr_check_enabled;
    uint8_t  ble_adv_tx_options;
    uint8_t  skip_unnecessary_checks_en;
    uint8_t  fast_conn_data_tx_en;
    int8_t   ch39_txpwr;
    uint8_t  adv_rsv_cnt;
    uint8_t  conn_rsv_cnt;
    uint8_t  priority_level_cfg;
    uint8_t  slv_fst_rx_lat_en;
    uint8_t  dl_itvl_phy_sync_en;
    uint8_t  scan_allow_adi_filter;
    uint8_t  enhanced_mem_resv;
    uint8_t  rxbuf_reserved;
    uint32_t config_magic;
} espb_config_t;

/** @brief How many NPL objects the controller will make (IDF's
 *         ble_npl_count_info_t). */
typedef struct {
    uint16_t evt_count;
    uint16_t evtq_count;
    uint16_t co_count;
    uint16_t sem_count;
    uint16_t mutex_count;
} espb_npl_counts_t;

/* NPL: every object is one pointer the port fills; time is milliseconds. */
typedef uint32_t ble_npl_time_t;
struct ble_npl_event    { void *event; };
struct ble_npl_eventq   { void *eventq; };
struct ble_npl_callout  { void *co; };
struct ble_npl_mutex    { void *mutex; };
struct ble_npl_sem      { void *sem; };
typedef void ble_npl_event_fn(struct ble_npl_event *ev);

#define BLE_NPL_TIME_FOREVER    0xFFFFFFFFUL

typedef enum {
    BLE_NPL_OK = 0, BLE_NPL_ENOMEM = 1, BLE_NPL_EINVAL = 2,
    BLE_NPL_INVALID_PARAM = 3, BLE_NPL_MEM_NOT_ALIGNED = 4,
    BLE_NPL_BAD_MUTEX = 5, BLE_NPL_TIMEOUT = 6, BLE_NPL_ERR_IN_ISR = 7,
    BLE_NPL_ERR_PRIV = 8, BLE_NPL_OS_NOT_STARTED = 9, BLE_NPL_ENOENT = 10,
    BLE_NPL_EBUSY = 11, BLE_NPL_ERROR = 12
} ble_npl_error_t;

/** @brief The NPL table, in the library's order. */
typedef struct {
    bool (*p_ble_npl_os_started)(void);
    void *(*p_ble_npl_get_current_task_id)(void);
    void (*p_ble_npl_eventq_init)(struct ble_npl_eventq *);
    void (*p_ble_npl_eventq_deinit)(struct ble_npl_eventq *);
    struct ble_npl_event *(*p_ble_npl_eventq_get)(struct ble_npl_eventq *,
                                                  ble_npl_time_t);
    void (*p_ble_npl_eventq_put)(struct ble_npl_eventq *,
                                 struct ble_npl_event *);
    void (*p_ble_npl_eventq_remove)(struct ble_npl_eventq *,
                                    struct ble_npl_event *);
    void (*p_ble_npl_event_run)(struct ble_npl_event *);
    bool (*p_ble_npl_eventq_is_empty)(struct ble_npl_eventq *);
    void (*p_ble_npl_event_init)(struct ble_npl_event *, ble_npl_event_fn *,
                                 void *);
    void (*p_ble_npl_event_deinit)(struct ble_npl_event *);
    void (*p_ble_npl_event_reset)(struct ble_npl_event *);
    bool (*p_ble_npl_event_is_queued)(struct ble_npl_event *);
    void *(*p_ble_npl_event_get_arg)(struct ble_npl_event *);
    void (*p_ble_npl_event_set_arg)(struct ble_npl_event *, void *);
    ble_npl_error_t (*p_ble_npl_mutex_init)(struct ble_npl_mutex *);
    ble_npl_error_t (*p_ble_npl_mutex_deinit)(struct ble_npl_mutex *);
    ble_npl_error_t (*p_ble_npl_mutex_pend)(struct ble_npl_mutex *,
                                            ble_npl_time_t);
    ble_npl_error_t (*p_ble_npl_mutex_release)(struct ble_npl_mutex *);
    ble_npl_error_t (*p_ble_npl_sem_init)(struct ble_npl_sem *, uint16_t);
    ble_npl_error_t (*p_ble_npl_sem_deinit)(struct ble_npl_sem *);
    ble_npl_error_t (*p_ble_npl_sem_pend)(struct ble_npl_sem *,
                                          ble_npl_time_t);
    ble_npl_error_t (*p_ble_npl_sem_release)(struct ble_npl_sem *);
    uint16_t (*p_ble_npl_sem_get_count)(struct ble_npl_sem *);
    int (*p_ble_npl_callout_init)(struct ble_npl_callout *,
                                  struct ble_npl_eventq *, ble_npl_event_fn *,
                                  void *);
    ble_npl_error_t (*p_ble_npl_callout_reset)(struct ble_npl_callout *,
                                               ble_npl_time_t);
    void (*p_ble_npl_callout_stop)(struct ble_npl_callout *);
    void (*p_ble_npl_callout_deinit)(struct ble_npl_callout *);
    void (*p_ble_npl_callout_mem_reset)(struct ble_npl_callout *);
    bool (*p_ble_npl_callout_is_active)(struct ble_npl_callout *);
    ble_npl_time_t (*p_ble_npl_callout_get_ticks)(struct ble_npl_callout *);
    uint32_t (*p_ble_npl_callout_remaining_ticks)(struct ble_npl_callout *,
                                                  ble_npl_time_t);
    void (*p_ble_npl_callout_set_arg)(struct ble_npl_callout *, void *);
    uint32_t (*p_ble_npl_time_get)(void);
    ble_npl_error_t (*p_ble_npl_time_ms_to_ticks)(uint32_t ms,
                                                  ble_npl_time_t *);
    ble_npl_error_t (*p_ble_npl_time_ticks_to_ms)(ble_npl_time_t,
                                                  uint32_t *);
    ble_npl_time_t (*p_ble_npl_time_ms_to_ticks32)(uint32_t);
    uint32_t (*p_ble_npl_time_ticks_to_ms32)(ble_npl_time_t);
    void (*p_ble_npl_time_delay)(ble_npl_time_t);
    void (*p_ble_npl_hw_set_isr)(int, uint32_t);
    uint32_t (*p_ble_npl_hw_enter_critical)(void);
    void (*p_ble_npl_hw_exit_critical)(uint32_t);
    uint32_t (*p_ble_npl_get_time_forever)(void);
    uint8_t (*p_ble_npl_hw_is_in_critical)(void);
    void (*p_ble_npl_eventq_put_to_front)(struct ble_npl_eventq *,
                                          struct ble_npl_event *);
} espb_npl_funcs_t;

/* os_mempool, as the controller-only build lays it out (no host fields). */
#define OS_MEMPOOL_F_EXT         0x01U
#define OS_MEMPOOL_F_FRAG        0x08U
#define OS_MEMPOOL_F_COMBINATION 0x80U

typedef enum {
    OS_OK = 0, OS_ENOMEM = 1, OS_EINVAL = 2, OS_INVALID_PARM = 3,
    OS_MEM_NOT_ALIGNED = 4
} os_error_t;

struct os_memblock {
    struct os_memblock *mb_next;
};

struct os_mempool {
    uint32_t            mp_block_size;
    uint16_t            mp_num_blocks;
    uint16_t            mp_num_free;
    uint16_t            mp_min_free;
    uint8_t             mp_flags;
    uint32_t            mp_membuf_addr;
    struct os_mempool  *mp_next;        /* STAILQ_ENTRY mp_list */
    struct os_memblock *mp_first;       /* the free list, SLIST_HEAD */
    const char         *name;
};

struct os_mempool_ext;
typedef os_error_t os_mempool_put_fn(struct os_mempool_ext *ome, void *data,
                                     void *arg);
typedef void *os_mempool_get_fn(struct os_mempool_ext *ome, void *arg);

struct os_mempool_ext {
    struct os_mempool  mpe_mp;
    os_mempool_put_fn *mpe_put_cb;
    void              *mpe_put_arg;
    os_mempool_get_fn *mpe_get_cb;
    void              *mpe_get_arg;
};

/* A chained buffer; the packet header follows the first one. */
struct os_mbuf {
    uint8_t        *om_data;
    uint8_t         om_flags;
    uint8_t         om_pkthdr_len;
    uint16_t        om_len;
    void           *om_omp;
    struct os_mbuf *om_next;
};

struct os_mbuf_pkthdr {
    uint16_t omp_len;
    uint16_t omp_flags;
    void    *omp_next;
};

/* Sizes and offsets the library reads, on the target (RV32). */
#if defined(__riscv) && __riscv_xlen == 32
_Static_assert(sizeof(espb_ext_funcs_t) == 48, "ext");
_Static_assert(offsetof(espb_ext_funcs_t, magic) == 44, "ext");
_Static_assert(sizeof(espb_config_t) == 120, "config");
_Static_assert(offsetof(espb_config_t, rtc_freq) == 24, "config");
_Static_assert(offsetof(espb_config_t, ptr_check_enabled) == 97, "config");
_Static_assert(offsetof(espb_config_t, config_magic) == 112, "config");
_Static_assert(sizeof(espb_npl_funcs_t) == 45 * 4, "npl");
_Static_assert(offsetof(espb_npl_funcs_t, p_ble_npl_hw_enter_critical) == 160,
               "npl");
_Static_assert(sizeof(struct os_mempool) == 28, "mempool");
_Static_assert(offsetof(struct os_mempool, mp_num_free) == 6, "mempool");
_Static_assert(offsetof(struct os_mempool, mp_flags) == 10, "mempool");
_Static_assert(offsetof(struct os_mempool, mp_first) == 20, "mempool");
_Static_assert(sizeof(struct os_mempool_ext) == 44, "mempool");
_Static_assert(sizeof(struct os_mbuf) == 16, "mbuf");
#endif

/* HCI through memory (VHCI): a command buffer's type, the room an ACL
 * buffer keeps ahead of its data. */
#define ESPB_HCI_BUF_CMD        3
#define ESPB_HCI_ACL_LEADING    4U

typedef int espb_hci_evt_fn(uint8_t *hci_ev, void *arg);
typedef int espb_hci_acl_fn(struct os_mbuf *om, void *arg);

/* The library's entry points. */
int esp_register_ext_funcs(espb_ext_funcs_t *funcs);
void esp_unregister_ext_funcs(void);
int esp_register_npl_funcs(espb_npl_funcs_t *funcs);
void esp_unregister_npl_funcs(void);
int r_ble_get_npl_element_info(espb_config_t *cfg, espb_npl_counts_t *out);
int ble_osi_coex_funcs_register(espb_coex_funcs_t *funcs);
int esp_ble_register_bb_funcs(void);
void esp_ble_unregister_bb_funcs(void);
char *ble_controller_get_compile_version(void);
bool esp_ble_controller_lib_check(void);
void esp_ble_controller_flash_only_param_config(void);
int r_ble_controller_init(espb_config_t *cfg);
int r_ble_controller_deinit(void);
int r_ble_controller_enable(uint8_t mode);
int r_ble_controller_disable(void);
int r_esp_ble_ll_set_public_addr(const uint8_t *addr);
int r_esp_ble_msys_init(uint16_t size1, uint16_t size2, uint16_t count1,
                        uint16_t count2, uint8_t from_heap);
void r_esp_ble_msys_deinit(void);

/* The controller's feature modules, in IDF's ble.c order. */
int base_stack_initEnv(void);
void base_stack_deinitEnv(void);
int base_stack_enable(void);
void base_stack_disable(void);
int adv_stack_initEnv(void);
void adv_stack_deinitEnv(void);
int adv_stack_enable(void);
void adv_stack_disable(void);
int extAdv_stack_initEnv(void);
void extAdv_stack_deinitEnv(void);
int extAdv_stack_enable(void);
void extAdv_stack_disable(void);
int scan_stack_initEnv(void);
void scan_stack_deinitEnv(void);
int scan_stack_enable(void);
void scan_stack_disable(void);
int sync_stack_initEnv(void);
void sync_stack_deinitEnv(void);
int sync_stack_enable(void);
void sync_stack_disable(void);
int conn_stack_initEnv(void);
void conn_stack_deinitEnv(void);
int conn_stack_enable(void);
void conn_stack_disable(void);

/* Duplicate filtering of advertising reports. */
void r_filter_duplicate_mode_enable(uint32_t mode);
void r_filter_duplicate_mode_disable(uint32_t mode);
void r_filter_duplicate_set_ring_list_max_num(uint32_t max_num);
void r_scan_duplicate_cache_refresh_set_time(uint32_t period_time);

/* HCI: the host's side of the controller's transport. */
void r_ble_hci_trans_cfg_hs(espb_hci_evt_fn *evt_cb, void *evt_arg,
                            espb_hci_acl_fn *acl_cb, void *acl_arg);
int r_ble_hci_trans_hs_cmd_tx(uint8_t *cmd);
int r_ble_hci_trans_hs_acl_tx(struct os_mbuf *om);
uint8_t *r_ble_hci_trans_buf_alloc(int type);
void r_ble_hci_trans_buf_free(uint8_t *buf);
struct os_mbuf *r_os_msys_get_pkthdr(uint16_t dsize, uint16_t user_hdr_len);
int r_os_mbuf_append(struct os_mbuf *om, const void *data, uint16_t len);
int r_os_mbuf_copydata(const struct os_mbuf *om, int off, int len,
                       void *dst);
int r_os_mbuf_free_chain(struct os_mbuf *om);

/* The baseband, from libbtbb. */
void bt_bb_v2_init_cmplx(uint8_t print_version);

#endif /* TIKU_DRV_WIFI_ESP_BLE_ABI_H_ */
