/*
 * Tiku Drivers - ESP32-C61 radio libraries, the ABI this driver uses
 *
 * Hand-written from ESP-IDF 4d59230 (OS adapter version 9), the tree whose
 * library commits fetch.sh pins: only what the driver calls or defines.
 * The asserts below hold the layouts to sizes and offsets measured from
 * IDF's own definitions compiled for RV32, never included here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_ESP_ABI_H_
#define TIKU_DRV_WIFI_ESP_ABI_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t esp_err_t;
#define ESP_OK                      0
#define ESP_FAIL                    (-1)
#define ESP_ERR_NO_MEM              0x101
#define ESP_ERR_NOT_FOUND           0x105
#define ESP_ERR_NVS_NOT_FOUND       0x1102

/* The adapter table's guard words and the init config's last field. */
#define ESP_WIFI_OS_ADAPTER_VERSION 0x00000009
#define ESP_WIFI_OS_ADAPTER_MAGIC   0xDEADBEAF
#define ESP_WIFI_CRYPTO_VERSION     0x00000001
#define WIFI_INIT_CONFIG_MAGIC      0x1F2F3F4F

/* A wait without end, and where a queue send puts its item. */
#define OSI_FUNCS_TIME_BLOCKING     0xFFFFFFFFUL
#define OSI_QUEUE_SEND_FRONT        0
#define OSI_QUEUE_SEND_BACK         1
#define OSI_QUEUE_SEND_OVERWRITE    2

/** @brief What the libraries need from an OS, in their order (C61 build). */
typedef struct {
    int32_t _version;
    bool (*_env_is_chip)(void);
    /* interrupts */
    void (*_set_intr)(int32_t cpu_no, uint32_t intr_source, uint32_t intr_num,
                      int32_t intr_prio);
    void (*_clear_intr)(uint32_t intr_source, uint32_t intr_num);
    void (*_set_isr)(int32_t n, void *f, void *arg);
    void (*_ints_on)(uint32_t mask);
    void (*_ints_off)(uint32_t mask);
    bool (*_is_from_isr)(void);
    void *(*_spin_lock_create)(void);
    void (*_spin_lock_delete)(void *lock);
    uint32_t (*_wifi_int_disable)(void *wifi_int_mux);
    void (*_wifi_int_restore)(void *wifi_int_mux, uint32_t tmp);
    void (*_task_yield_from_isr)(void);
    /* semaphores and mutexes */
    void *(*_semphr_create)(uint32_t max, uint32_t init);
    void (*_semphr_delete)(void *semphr);
    int32_t (*_semphr_take)(void *semphr, uint32_t block_time_tick);
    int32_t (*_semphr_give)(void *semphr);
    void *(*_wifi_thread_semphr_get)(void);
    void *(*_mutex_create)(void);
    void *(*_recursive_mutex_create)(void);
    void (*_mutex_delete)(void *mutex);
    int32_t (*_mutex_lock)(void *mutex);
    int32_t (*_mutex_unlock)(void *mutex);
    /* queues and event groups */
    void *(*_queue_create)(uint32_t queue_len, uint32_t item_size);
    void (*_queue_delete)(void *queue);
    int32_t (*_queue_send)(void *queue, void *item, uint32_t block_time_tick);
    int32_t (*_queue_send_from_isr)(void *queue, void *item, void *hptw);
    int32_t (*_queue_send_to_back)(void *queue, void *item,
                                   uint32_t block_time_tick);
    int32_t (*_queue_send_to_front)(void *queue, void *item,
                                    uint32_t block_time_tick);
    int32_t (*_queue_recv)(void *queue, void *item, uint32_t block_time_tick);
    uint32_t (*_queue_msg_waiting)(void *queue);
    void *(*_event_group_create)(void);
    void (*_event_group_delete)(void *event);
    uint32_t (*_event_group_set_bits)(void *event, uint32_t bits);
    uint32_t (*_event_group_clear_bits)(void *event, uint32_t bits);
    uint32_t (*_event_group_wait_bits)(void *event, uint32_t bits_to_wait_for,
                                       int clear_on_exit,
                                       int wait_for_all_bits,
                                       uint32_t block_time_tick);
    /* tasks */
    int32_t (*_task_create_pinned_to_core)(void *task_func, const char *name,
                                           uint32_t stack_depth, void *param,
                                           uint32_t prio, void *task_handle,
                                           uint32_t core_id);
    int32_t (*_task_create)(void *task_func, const char *name,
                            uint32_t stack_depth, void *param, uint32_t prio,
                            void *task_handle);
    void (*_task_delete)(void *task_handle);
    void (*_task_delay)(uint32_t tick);
    int32_t (*_task_ms_to_tick)(uint32_t ms);
    void *(*_task_get_current_task)(void);
    int32_t (*_task_get_max_priority)(void);
    /* memory, events, randomness */
    void *(*_malloc)(size_t size);
    void (*_free)(void *p);
    int32_t (*_event_post)(const char *event_base, int32_t event_id,
                           void *event_data, size_t event_data_size,
                           uint32_t ticks_to_wait);
    uint32_t (*_get_free_heap_size)(void);
    uint32_t (*_rand)(void);
    void (*_dport_access_stall_other_cpu_start_wrap)(void);
    void (*_dport_access_stall_other_cpu_end_wrap)(void);
    void (*_wifi_pm_sleep_lock_acquire)(void);
    void (*_wifi_pm_sleep_lock_release)(void);
    /* PHY, MAC address, timers, clocks */
    void (*_phy_disable)(void);
    void (*_phy_enable)(void);
    int (*_phy_update_country_info)(const char *country);
    int (*_read_mac)(uint8_t *mac, unsigned int type);
    void (*_timer_arm)(void *timer, uint32_t tmout, bool repeat);
    void (*_timer_disarm)(void *timer);
    void (*_timer_done)(void *ptimer);
    void (*_timer_setfn)(void *ptimer, void *pfunction, void *parg);
    void (*_timer_arm_us)(void *ptimer, uint32_t us, bool repeat);
    void (*_wifi_reset_mac)(void);
    void (*_wifi_clock_enable)(void);
    void (*_wifi_clock_disable)(void);
    void (*_wifi_rtc_enable_iso)(void);
    void (*_wifi_rtc_disable_iso)(void);
    int64_t (*_esp_timer_get_time)(void);
    /* NVS */
    int (*_nvs_set_i8)(uint32_t handle, const char *key, int8_t value);
    int (*_nvs_get_i8)(uint32_t handle, const char *key, int8_t *out_value);
    int (*_nvs_set_u8)(uint32_t handle, const char *key, uint8_t value);
    int (*_nvs_get_u8)(uint32_t handle, const char *key, uint8_t *out_value);
    int (*_nvs_set_u16)(uint32_t handle, const char *key, uint16_t value);
    int (*_nvs_get_u16)(uint32_t handle, const char *key, uint16_t *out_value);
    int (*_nvs_open)(const char *name, unsigned int open_mode,
                     uint32_t *out_handle);
    void (*_nvs_close)(uint32_t handle);
    int (*_nvs_commit)(uint32_t handle);
    int (*_nvs_set_blob)(uint32_t handle, const char *key, const void *value,
                         size_t length);
    int (*_nvs_get_blob)(uint32_t handle, const char *key, void *out_value,
                         size_t *length);
    int (*_nvs_erase_key)(uint32_t handle, const char *key);
    /* randomness, time, logging */
    int (*_get_random)(uint8_t *buf, size_t len);
    int (*_get_time)(void *t);
    unsigned long (*_random)(void);
    uint32_t (*_slowclk_cal_get)(void);
    void (*_log_write)(unsigned int level, const char *tag,
                       const char *format, ...);
    void (*_log_writev)(unsigned int level, const char *tag,
                        const char *format, va_list args);
    uint32_t (*_log_timestamp)(void);
    /* allocation, internal RAM and anywhere */
    void *(*_malloc_internal)(size_t size);
    void *(*_realloc_internal)(void *ptr, size_t size);
    void *(*_calloc_internal)(size_t n, size_t size);
    void *(*_zalloc_internal)(size_t size);
    void *(*_wifi_malloc)(size_t size);
    void *(*_wifi_realloc)(void *ptr, size_t size);
    void *(*_wifi_calloc)(size_t n, size_t size);
    void *(*_wifi_zalloc)(size_t size);
    void *(*_wifi_create_queue)(int queue_len, int item_size);
    void (*_wifi_delete_queue)(void *queue);
    /* coexistence with BLE (not built: stubs) */
    int (*_coex_init)(void);
    void (*_coex_deinit)(void);
    int (*_coex_enable)(void);
    void (*_coex_disable)(void);
    uint32_t (*_coex_status_get)(void);
    void (*_coex_condition_set)(uint32_t type, bool dissatisfy);
    int (*_coex_wifi_request)(uint32_t event, uint32_t latency,
                              uint32_t duration);
    int (*_coex_wifi_release)(uint32_t event);
    int (*_coex_wifi_channel_set)(uint8_t primary, uint8_t secondary);
    int (*_coex_event_duration_get)(uint32_t event, uint32_t *duration);
    int (*_coex_pti_get)(uint32_t event, uint8_t *pti);
    void (*_coex_schm_status_bit_clear)(uint32_t type, uint32_t status);
    void (*_coex_schm_status_bit_set)(uint32_t type, uint32_t status);
    int (*_coex_schm_interval_set)(uint32_t interval);
    uint32_t (*_coex_schm_interval_get)(void);
    uint8_t (*_coex_schm_curr_period_get)(void);
    void *(*_coex_schm_curr_phase_get)(void);
    int (*_coex_schm_process_restart)(void);
    int (*_coex_schm_register_cb)(int type, int (*cb)(int));
    int (*_coex_register_start_cb)(int (*cb)(void));
    /* sleep retention (C6 family), then the rest of coexistence */
    void (*_regdma_link_set_write_wait_content)(void *link, uint32_t value,
                                                uint32_t mask);
    void *(*_sleep_retention_find_link_by_id)(int id);
    int (*_coex_schm_flexible_period_set)(uint8_t period);
    uint8_t (*_coex_schm_flexible_period_get)(void);
    void *(*_coex_schm_get_phase_by_idx)(int phase_idx);
    bool (*_wifi_disable_ac_ax)(void);
    int32_t (*_wifi_bb_sleep_retention_attach)(void);
    int32_t (*_wifi_bb_sleep_retention_detach)(void);
    int32_t (*_wifi_mac_sleep_retention_attach)(void);
    int32_t (*_wifi_mac_sleep_retention_detach)(void);
    int32_t _magic;
} wifi_osi_funcs_t;

/** @brief The crypto the libraries call for themselves, handed over at init:
 *         AES-128 is CBC over whole blocks, the wraps count 8-byte blocks of
 *         key, and the CCMP calls return a buffer from the heap, or NULL. */
typedef struct {
    uint32_t size;
    uint32_t version;
    int (*hmac_sha256_vector)(const unsigned char *key, int key_len,
                              int num_elem, const unsigned char *addr[],
                              const int *len, unsigned char *mac);
    int (*pbkdf2_sha1)(const char *passphrase, const char *ssid,
                       unsigned int ssid_len, int iterations,
                       unsigned char *buf, unsigned int buflen);
    int (*aes_128_encrypt)(const unsigned char *key, const unsigned char *iv,
                           unsigned char *data, int data_len);
    int (*aes_128_decrypt)(const unsigned char *key, const unsigned char *iv,
                           unsigned char *data, int data_len);
    int (*omac1_aes_128)(const uint8_t *key, const uint8_t *data,
                         size_t data_len, uint8_t *mic);
    uint8_t *(*ccmp_decrypt)(const uint8_t *tk, const uint8_t *hdr,
                             const uint8_t *data, size_t data_len,
                             size_t *decrypted_len, bool espnow_pkt);
    uint8_t *(*ccmp_encrypt)(const uint8_t *tk, uint8_t *frame, size_t len,
                             size_t hdrlen, uint8_t *pn, int keyid,
                             size_t *encrypted_len);
    int (*aes_gmac)(const uint8_t *key, size_t keylen, const uint8_t *iv,
                    size_t iv_len, const uint8_t *aad, size_t aad_len,
                    uint8_t *mic);
    int (*sha256_vector)(size_t num_elem, const uint8_t *addr[],
                         const size_t *len, uint8_t *buf);
    int (*aes_wrap)(const unsigned char *kek, size_t kek_len, int n,
                    const unsigned char *plain, unsigned char *cipher);
    int (*aes_unwrap)(const unsigned char *kek, size_t kek_len, int n,
                      const unsigned char *cipher, unsigned char *plain);
} wpa_crypto_funcs_t;

/** @brief esp_wifi_init_internal()'s argument: buffers, features, tables. */
typedef struct {
    wifi_osi_funcs_t   *osi_funcs;
    wpa_crypto_funcs_t  wpa_crypto_funcs;
    int                 static_rx_buf_num;
    int                 dynamic_rx_buf_num;
    int                 tx_buf_type;
    int                 static_tx_buf_num;
    int                 dynamic_tx_buf_num;
    int                 rx_mgmt_buf_type;
    int                 rx_mgmt_buf_num;
    int                 cache_tx_buf_num;
    int                 csi_enable;
    int                 ampdu_rx_enable;
    int                 ampdu_tx_enable;
    int                 amsdu_tx_enable;
    int                 nvs_enable;
    int                 nano_enable;
    int                 rx_ba_win;
    int                 wifi_task_core_id;
    int                 beacon_max_len;
    int                 mgmt_sbuf_num;
    uint64_t            feature_caps;
    bool                sta_disconnected_pm;
    int                 espnow_max_encrypt_num;
    int                 tx_hetb_queue_num;
    bool                dump_hesigb_enable;
    bool                privacy_enhancements;
    uint8_t             rmac_auto_reset_int;
    int                 wifi_task_stack_size;
    int                 magic;
} wifi_init_config_t;

/* Measured from IDF's definitions for RV32 (temp/esp32c61/abicheck); a host
 * analysis, with its wider pointers, has nothing to compare. */
#if defined(__riscv) && __riscv_xlen == 32
_Static_assert(sizeof(wifi_osi_funcs_t) == 508, "OS adapter size");
_Static_assert(offsetof(wifi_osi_funcs_t, _semphr_create) == 52, "osi");
_Static_assert(offsetof(wifi_osi_funcs_t, _task_create) == 148, "osi");
_Static_assert(offsetof(wifi_osi_funcs_t, _phy_enable) == 212, "osi");
_Static_assert(offsetof(wifi_osi_funcs_t, _timer_arm) == 224, "osi");
_Static_assert(offsetof(wifi_osi_funcs_t, _nvs_open) == 292, "osi");
_Static_assert(offsetof(wifi_osi_funcs_t, _slowclk_cal_get) == 328, "osi");
_Static_assert(offsetof(wifi_osi_funcs_t, _malloc_internal) == 344, "osi");
_Static_assert(offsetof(wifi_osi_funcs_t, _coex_init) == 384, "osi");
_Static_assert(offsetof(wifi_osi_funcs_t,
                        _regdma_link_set_write_wait_content) == 464, "osi");
_Static_assert(offsetof(wifi_osi_funcs_t, _wifi_disable_ac_ax) == 484, "osi");
_Static_assert(offsetof(wifi_osi_funcs_t, _magic) == 504, "osi");
_Static_assert(sizeof(wpa_crypto_funcs_t) == 52, "crypto table size");
_Static_assert(sizeof(wifi_init_config_t) == 160, "init config size");
_Static_assert(offsetof(wifi_init_config_t, feature_caps) == 128, "init");
_Static_assert(offsetof(wifi_init_config_t, wifi_task_stack_size) == 152,
               "init");
_Static_assert(offsetof(wifi_init_config_t, magic) == 156, "init");
#endif

/** @brief _read_mac()'s kinds; the driver answers the first two. */
#define ESP_MAC_WIFI_STA            0U
#define ESP_MAC_WIFI_SOFTAP         1U

/** @brief The PHY's 128 bytes of defaults, and what calibration keeps. */
typedef struct {
    uint8_t params[128];
} esp_phy_init_data_t;

typedef struct {
    uint8_t version[4];
    uint8_t mac[6];
    uint8_t opaque[1894];
} esp_phy_calibration_data_t;

typedef enum {
    PHY_RF_CAL_PARTIAL = 0,
    PHY_RF_CAL_NONE    = 1,
    PHY_RF_CAL_FULL    = 2
} esp_phy_calibration_mode_t;

/** @brief libphy: the PHY's version, formatted into its own buffer. */
const char *get_phy_version_str(void);

/** @brief libphy: first bring-up, calibrating as @p mode says. */
int register_chipv7_phy(const esp_phy_init_data_t *init_data,
                        esp_phy_calibration_data_t *cal_data,
                        esp_phy_calibration_mode_t mode);

/** @brief libphy: 1 when the PHY is shared with BLE (it is, on this chip). */
void phy_init_param_set(uint8_t param);

/** @brief libphy: back up from the state the last close left. */
void phy_wakeup_init(void);

/** @brief libphy: RF off, the temperature sensor off, hopping settled. */
void phy_close_rf(void);
void phy_xpd_tsens(void);
void phy_wait_freq_hw_hop_done(void);

/** @brief libphy: whether Wi-Fi is the PHY's user now. */
void phy_wifi_enable_set(uint8_t enable);

/** @brief libphy: the baseband watchdog's checks. */
void set_bb_wdg(bool busy_chk, bool srch_chk, uint16_t max_busy,
                uint16_t max_srch, bool rst_en, bool int_en, bool clr);

/** @brief libphy: follow the die's temperature (PLL, power, calibration). */
void phy_param_track_tot(bool en_wifi, bool en_ble_154);

/** @brief Each library's git revision, set at its build. */
extern const char *libnet80211_reversion_git;
extern const char *libpp_reversion_git;
extern const char *libcore_reversion_git;

/** @brief libnet80211: start the stack from @p config. */
esp_err_t esp_wifi_init_internal(const wifi_init_config_t *config);

/** @brief libnet80211: the stack's teardown, once stopped. */
esp_err_t esp_wifi_deinit_internal(void);

typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA,
    WIFI_MODE_AP,
    WIFI_MODE_APSTA,
    WIFI_MODE_NAN
} wifi_mode_t;

typedef enum {
    WIFI_IF_STA = 0,
    WIFI_IF_AP
} wifi_interface_t;

/** @brief libnet80211: the station's API, as far as the driver uses it. */
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6]);
esp_err_t esp_wifi_internal_set_log_level(int level);

/** @brief One access point a scan found: the fields the driver reads, the
 *         rest (ciphers, PHY modes, country, HE, bandwidth) kept opaque. */
typedef struct {
    uint8_t  bssid[6];
    uint8_t  ssid[33];          /* NUL-terminated */
    uint8_t  primary;           /* channel */
    uint32_t second;            /* the secondary channel's side */
    int8_t   rssi;
    uint32_t authmode;
    uint8_t  rest[40];
} wifi_ap_record_t;

/** @brief WIFI_EVENT_SCAN_DONE's data: 0 on success, and the APs found. */
typedef struct {
    uint32_t status;
    uint8_t  number;
    uint8_t  scan_id;
} wifi_event_sta_scan_done_t;

/** @brief A country's rule: a channel range, its widest band (2 = 40 MHz)
 *         and its power cap in dBm EIRP.  Two rules at most here. */
typedef struct {
    uint8_t  start_channel;
    uint8_t  end_channel;
    uint16_t max_bandwidth : 3;
    uint16_t max_eirp : 6;
    uint16_t is_dfs : 1;
    uint16_t reserved : 6;
} wifi_reg_rule_t;

typedef struct {
    uint8_t         n_reg_rules;
    wifi_reg_rule_t reg_rules[2];
} wifi_regulatory_t;

/** @brief A country code and the rules it follows; "##" ends the table. */
typedef struct {
    char    cn[2];
    uint8_t regulatory_type;
} wifi_regdomain_t;

#if defined(__riscv) && __riscv_xlen == 32
_Static_assert(sizeof(wifi_ap_record_t) == 92, "AP record size");
_Static_assert(offsetof(wifi_ap_record_t, ssid) == 6, "ap");
_Static_assert(offsetof(wifi_ap_record_t, primary) == 39, "ap");
_Static_assert(offsetof(wifi_ap_record_t, rssi) == 44, "ap");
_Static_assert(offsetof(wifi_ap_record_t, authmode) == 48, "ap");
_Static_assert(sizeof(wifi_event_sta_scan_done_t) == 8, "scan done size");
_Static_assert(sizeof(wifi_reg_rule_t) == 4, "rule size");
_Static_assert(sizeof(wifi_regulatory_t) == 10, "regulatory size");
_Static_assert(sizeof(wifi_regdomain_t) == 3, "regdomain size");
#endif

/** @brief libnet80211: scan every channel the country allows (a NULL
 *         config: active, default dwell), done with WIFI_EVENT_SCAN_DONE. */
esp_err_t esp_wifi_scan_start(const void *config, bool block);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number);

/** @brief libnet80211: up to @p number records, then the stack's list freed. */
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number,
                                       wifi_ap_record_t *ap_records);
esp_err_t esp_wifi_clear_ap_list(void);

/* What the supplicant reports of an RSN or WPA element: the protocol, the
 * key management as its own bits, the ciphers as wifi_cipher_type_t. */
#define WPA_PROTO_WPA               (1U << 0)
#define WPA_PROTO_RSN               (1U << 1)

#define WPA_KEY_MGMT_IEEE8021X      (1U << 0)
#define WPA_KEY_MGMT_PSK            (1U << 1)
#define WPA_KEY_MGMT_FT_IEEE8021X   (1U << 5)
#define WPA_KEY_MGMT_FT_PSK         (1U << 6)
#define WPA_KEY_MGMT_IEEE8021X_SHA256 (1U << 7)
#define WPA_KEY_MGMT_PSK_SHA256     (1U << 8)
#define WPA_KEY_MGMT_SAE            (1U << 10)
#define WPA_KEY_MGMT_FT_SAE         (1U << 11)
#define WPA_KEY_MGMT_SUITE_B        (1U << 16)
#define WPA_KEY_MGMT_SUITE_B_192    (1U << 17)
#define WPA_KEY_MGMT_OWE            (1U << 22)
#define WPA_KEY_MGMT_FT_IEEE8021X_SHA384 (1U << 24)
#define WPA_KEY_MGMT_SAE_EXT_KEY    (1U << 26)

typedef enum {
    WIFI_CIPHER_TYPE_NONE = 0,
    WIFI_CIPHER_TYPE_WEP40,
    WIFI_CIPHER_TYPE_WEP104,
    WIFI_CIPHER_TYPE_TKIP,
    WIFI_CIPHER_TYPE_CCMP,
    WIFI_CIPHER_TYPE_TKIP_CCMP,
    WIFI_CIPHER_TYPE_AES_CMAC128,
    WIFI_CIPHER_TYPE_SMS4,
    WIFI_CIPHER_TYPE_GCMP,
    WIFI_CIPHER_TYPE_GCMP256,
    WIFI_CIPHER_TYPE_AES_GMAC128,
    WIFI_CIPHER_TYPE_AES_GMAC256,
    WIFI_CIPHER_TYPE_UNKNOWN
} wifi_cipher_type_t;

typedef struct {
    int            proto;
    int            pairwise_cipher;
    int            group_cipher;
    int            key_mgmt;
    int            capabilities;
    size_t         num_pmkid;
    const uint8_t *pmkid;
    int            mgmt_group_cipher;
    uint8_t        rsnxe_capa;
} wifi_wpa_ie_t;

/** @brief The supplicant as the stack calls it: station, soft-AP, WPA3 and
 *         OWE hooks; a NULL entry is one this supplicant does not do. */
typedef struct {
    bool (*wpa_sta_init)(void);
    bool (*wpa_sta_deinit)(void);
    int (*wpa_sta_connect)(uint8_t *bssid);
    void (*wpa_sta_connected_cb)(uint8_t *bssid);
    void (*wpa_sta_disconnected_cb)(uint8_t reason_code);
    int (*wpa_sta_rx_eapol)(uint8_t *src_addr, uint8_t *buf, uint32_t len);
    bool (*wpa_sta_in_4way_handshake)(void);
    void *(*wpa_ap_init)(void);
    bool (*wpa_ap_deinit)(void *data);
    bool (*wpa_ap_join)(void *join);
    bool (*wpa_ap_remove)(uint8_t *bssid);
    uint8_t *(*wpa_ap_get_wpa_ie)(size_t *len);
    bool (*wpa_ap_rx_eapol)(void *hapd_data, void *sm, uint8_t *data,
                            size_t data_len);
    void (*wpa_ap_get_peer_spp_msg)(void *sm, bool *spp_cap, bool *spp_req);
    char *(*wpa_config_parse_string)(const char *value, size_t *len);
    int (*wpa_parse_wpa_ie)(const uint8_t *wpa_ie, size_t wpa_ie_len,
                            wifi_wpa_ie_t *data);
    int (*wpa_config_bss)(uint8_t *bssid);
    int (*wpa_michael_mic_failure)(uint16_t is_unicast);
    uint8_t *(*wpa3_build_sae_msg)(uint8_t *bssid, uint32_t type,
                                   size_t *len);
    int (*wpa3_parse_sae_msg)(uint8_t *buf, size_t len, uint32_t type,
                              uint16_t status);
    int (*wpa3_hostap_handle_auth)(uint8_t *buf, size_t len, uint32_t type,
                                   uint16_t status, uint8_t *bssid);
    int (*wpa_sta_rx_mgmt)(uint8_t type, uint8_t *frame, size_t len,
                           uint8_t *sender, int8_t rssi, uint8_t channel,
                           uint64_t current_tsf);
    void (*wpa_config_done)(void);
    uint8_t *(*owe_build_dhie)(uint16_t group);
    int (*owe_process_assoc_resp)(const uint8_t *rsn_ie, size_t rsn_len,
                                  const uint8_t *dh_ie, size_t dh_len);
    void (*wpa_sta_clear_curr_pmksa)(void);
    void (*wpa_config_reload)(void);
    int (*wpa_parse_wpa_ie_scan_only)(const uint8_t *wpa_ie,
                                      size_t wpa_ie_len, wifi_wpa_ie_t *data);
} wpa_funcs_t;

#if defined(__riscv) && __riscv_xlen == 32
_Static_assert(sizeof(wifi_wpa_ie_t) == 36, "wpa ie data size");
_Static_assert(offsetof(wifi_wpa_ie_t, pmkid) == 24, "wpa ie");
_Static_assert(sizeof(wpa_funcs_t) == 112, "supplicant table size");
_Static_assert(offsetof(wpa_funcs_t, wpa_parse_wpa_ie) == 60, "wpa");
_Static_assert(offsetof(wpa_funcs_t, wpa_sta_rx_mgmt) == 84, "wpa");
#endif

/** @brief libnet80211: hand it the supplicant (it frees the table at
 *         unregister, through the adapter's free). */
int esp_wifi_register_wpa_cb_internal(wpa_funcs_t *cb);
int esp_wifi_unregister_wpa_cb_internal(void);

/** @brief The station's configuration: the fields the driver sets, the
 *         rest (PMF, SAE, HE, retry details) left zero. */
typedef struct {
    uint8_t  ssid[32];
    uint8_t  password[64];
    uint32_t scan_method;
    bool     bssid_set;
    uint8_t  bssid[6];
    uint8_t  channel;
    uint16_t listen_interval;
    uint32_t sort_method;
    struct {
        int8_t   rssi;
        uint32_t authmode;          /* the weakest the station accepts */
        uint8_t  rssi_5g_adjustment;
    } threshold;
    struct {
        bool capable;
        bool required;
    } pmf_cfg;
    uint32_t features;              /* WIFI_STA_* bits */
    uint8_t  rest[48];
} wifi_sta_config_t;

#define WIFI_ALL_CHANNEL_SCAN       1U      /* scan_method: every channel */
#define WIFI_CONNECT_AP_BY_SIGNAL   0U      /* sort_method: strongest first */
#define WIFI_STA_NO_WPA3_COMPAT     (1U << 6)   /* no RSN override elements */

typedef union {
    wifi_sta_config_t sta;
    uint8_t           raw[184];
} wifi_config_t;

#define WIFI_AUTH_OPEN              0U
#define WIFI_AUTH_WPA_PSK           2U
#define WIFI_AUTH_WPA2_PSK          3U
#define WIFI_AUTH_WPA_WPA2_PSK      4U
#define WIFI_AUTH_WPA2_WPA3_PSK     7U

typedef struct {
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    uint8_t  bssid[6];
    uint8_t  channel;
    uint32_t authmode;
    uint16_t aid;
} wifi_event_sta_connected_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t bssid[6];
    uint8_t reason;
    int8_t  rssi;
} wifi_event_sta_disconnected_t;

#if defined(__riscv) && __riscv_xlen == 32
_Static_assert(sizeof(wifi_config_t) == 184, "station config size");
_Static_assert(offsetof(wifi_sta_config_t, password) == 32, "sta");
_Static_assert(offsetof(wifi_sta_config_t, scan_method) == 96, "sta");
_Static_assert(offsetof(wifi_sta_config_t, listen_interval) == 108, "sta");
_Static_assert(offsetof(wifi_sta_config_t, threshold) == 116, "sta");
_Static_assert(offsetof(wifi_sta_config_t, pmf_cfg) == 128, "sta");
_Static_assert(offsetof(wifi_sta_config_t, features) == 132, "sta");
_Static_assert(sizeof(wifi_event_sta_connected_t) == 48, "connected size");
_Static_assert(offsetof(wifi_event_sta_connected_t, authmode) == 40, "conn");
_Static_assert(sizeof(wifi_event_sta_disconnected_t) == 41, "disc size");
_Static_assert(offsetof(wifi_event_sta_disconnected_t, reason) == 39, "disc");
#endif

/** @brief libnet80211: configure, join and leave (IDF's esp_wifi_connect
 *         and _disconnect wrap the two internal calls).  PMF is on unless
 *         turned off after each configure: this supplicant does no BIP. */
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf);
esp_err_t esp_wifi_disable_pmf_config(wifi_interface_t interface);
esp_err_t esp_wifi_connect_internal(void);
esp_err_t esp_wifi_disconnect_internal(void);
esp_err_t esp_wifi_sta_get_rssi(int *rssi);

/* Disconnect reasons the driver tells apart. */
#define WIFI_REASON_ASSOC_LEAVE     8U

/* What the stack offers its supplicant: the profile being joined, the
 * association's element, EAPOL out, keys in, and the handshake's end. */
#define WIFI_APPIE_WPA              3U
#define WIFI_APPIE_RSN              4U
#define NONE_AUTH                   0x01U   /* the profile's auth mode */
#define WPA2_AUTH_PSK               0x05U
#define WIFI_WPA_ALG_TKIP           2
#define WIFI_WPA_ALG_CCMP           3
#define KEY_FLAG_RX                 (1 << 2)
#define KEY_FLAG_TX                 (1 << 3)
#define KEY_FLAG_GROUP              (1 << 4)
#define KEY_FLAG_PAIRWISE           (1 << 5)

struct wifi_ssid {
    int     len;
    uint8_t ssid[32];
};

/** @brief After an EAPOL frame left: its payload, and whether it failed. */
typedef void (*eapol_txcb_t)(uint8_t *eapol, size_t len, bool tx_failure);

esp_err_t esp_wifi_sta_connect_internal(const uint8_t *bssid);
int esp_wifi_set_appie_internal(uint8_t type, uint8_t *ie, uint16_t len,
                                uint8_t flag);
int esp_wifi_unset_appie_internal(uint8_t type);
int esp_wifi_internal_tx(wifi_interface_t wifi_if, void *buffer,
                         uint16_t len);
int esp_wifi_set_sta_key_internal(int alg, uint8_t *addr, int key_idx,
                                  int set_tx, uint8_t *seq, size_t seq_len,
                                  uint8_t *key, size_t key_len, int key_flag);
bool esp_wifi_auth_done_internal(void);
int esp_wifi_register_eapol_txdonecb_internal(eapol_txcb_t fn);
uint8_t *esp_wifi_sta_get_ap_info_prof_pmk_internal(void);
uint8_t *esp_wifi_sta_get_prof_password_internal(void);
struct wifi_ssid *esp_wifi_sta_get_prof_ssid_internal(void);
uint8_t esp_wifi_sta_get_reset_nvs_pmk_internal(void);
uint8_t esp_wifi_sta_set_reset_nvs_pmk_internal(uint8_t reset_flag);
int esp_wifi_sta_update_ap_info_internal(void);
bool esp_wifi_sta_prof_is_rsn_internal(void);
uint8_t esp_wifi_sta_get_prof_authmode_internal(void);
int esp_wifi_get_macaddr_internal(uint8_t if_index, uint8_t *macaddr);
void esp_wifi_deauthenticate_internal(uint8_t reason_code);
uint8_t esp_wifi_sta_get_pairwise_cipher_internal(void);
uint8_t esp_wifi_sta_get_group_cipher_internal(void);

/** @brief libnet80211/libpp: the station's frames.  A received one is an
 *         Ethernet II frame the stack lends until its @p eb is freed; one
 *         sent is copied (esp_wifi_internal_tx). */
typedef esp_err_t (*wifi_rxcb_t)(void *buffer, uint16_t len, void *eb);
esp_err_t esp_wifi_internal_reg_rxcb(wifi_interface_t ifx, wifi_rxcb_t fn);
void esp_wifi_internal_free_rx_buffer(void *eb);

/** @brief The base the stack posts its events under (esp_glue.c). */
extern const char *WIFI_EVENT;

/* The events the driver follows, under WIFI_EVENT. */
#define WIFI_EVENT_WIFI_READY       0
#define WIFI_EVENT_SCAN_DONE        1
#define WIFI_EVENT_STA_START        2
#define WIFI_EVENT_STA_STOP         3
#define WIFI_EVENT_STA_CONNECTED    4
#define WIFI_EVENT_STA_DISCONNECTED 5

#endif /* TIKU_DRV_WIFI_ESP_ABI_H_ */
