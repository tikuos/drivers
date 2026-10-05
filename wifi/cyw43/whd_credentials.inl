/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * whd_credentials.inl - CYW43439 saved Wi-Fi profile in durable memory.
 *
 * Keeps the last profile that joined so the runner can rejoin at cold boot.
 * Included by whd.c; TikuBench compiles it on the host with NVM doubles.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** Record magic, "WIFI" in ASCII. */
#define WIFI_CRED_MAGIC  0x57494649UL

/** Shortest passphrase a WPA2-PSK join accepts. */
#define WIFI_PSK_MIN_LEN 8U

/** Shortest password a WPA3-SAE join accepts. */
#define WIFI_SAE_MIN_LEN 1U

typedef struct {
    uint32_t magic;          /* WIFI_CRED_MAGIC when populated */
    uint8_t  ssid_len;       /* 0 = empty / forgotten          */
    uint8_t  auth_flavor;    /* 0 = WPA2-PSK, 1 = WPA3-SAE     */
    uint8_t  _pad[2];
    char     ssid[33];       /* null-terminated, 32 + NUL      */
    char     psk[64];        /* null-terminated, 63 + NUL      */
    uint32_t xor_check;      /* xor of all bytes above (sanity) */
} wifi_cred_persist_t;

/* The record caps every passphrase at TIKU_WIRELESS_PSK_MAX bytes, so this
 * radio refuses a longer WPA3-SAE password although SAE itself allows one. */
_Static_assert(sizeof(((wifi_cred_persist_t *)0)->ssid) ==
               TIKU_WIRELESS_SSID_MAX + 1U,
               "credential record SSID field out of step with the interface");
_Static_assert(sizeof(((wifi_cred_persist_t *)0)->psk) ==
               TIKU_WIRELESS_PSK_MAX + 1U,
               "credential record passphrase field out of step");

static TIKU_DURABLE wifi_cred_persist_t wifi_cred_nvm;

/** Result of the last save or forget this boot; TIKU_DRV_OK until one fails. */
static int wifi_store_result;

/**
 * @brief Compute XOR checksum for a credential record
 *
 * Compute simple XOR checksum over everything except xor_check
 * itself. Rejects some corruption; it is not authentication and does
 * not make a write atomic.
 *
 * @param c  Credential record to checksum
 * @return XOR checksum across bytes preceding the xor_check field
 */
static uint32_t wifi_cred_xor(const wifi_cred_persist_t *c)
{
    const uint8_t *p = (const uint8_t *)c;
    uint32_t x = 0UL;
    size_t   i;
    size_t   n = (size_t)((const uint8_t *)&c->xor_check - p);

    for (i = 0U; i < n; ++i) {
        x ^= ((uint32_t)p[i] << ((i & 3U) * 8U));
    }
    return x;
}

/**
 * @brief Measure and validate an SSID and passphrase pair.
 *
 * Shared by live requests and by the record load, so both apply the same
 * bounds: SSID 1..32, WPA2-PSK passphrase 8..63, WPA3-SAE password 1..63.
 *
 * @param ssid  SSID, NUL-terminated
 * @param psk   Passphrase, NUL-terminated
 * @param auth  0 = WPA2-PSK, 1 = WPA3-SAE
 * @param slen  Receives the SSID length on success
 * @param plen  Receives the passphrase length on success
 * @return 1 if the pair is valid, 0 otherwise
 */
static int wifi_profile_lengths(const char *ssid, const char *psk,
                                uint8_t auth, uint8_t *slen, uint8_t *plen)
{
    unsigned s = 0U;
    unsigned p = 0U;
    unsigned min_len;

    if (ssid == NULL || psk == NULL ||
        auth > (uint8_t)TIKU_WIRELESS_AUTH_WPA3_SAE) {
        return 0;
    }
    while (s <= TIKU_WIRELESS_SSID_MAX && ssid[s] != '\0') {
        s++;
    }
    while (p <= TIKU_WIRELESS_PSK_MAX && psk[p] != '\0') {
        p++;
    }
    min_len = (auth == (uint8_t)TIKU_WIRELESS_AUTH_WPA3_SAE) ?
              WIFI_SAE_MIN_LEN : WIFI_PSK_MIN_LEN;
    if (s == 0U || s > TIKU_WIRELESS_SSID_MAX ||
        p > TIKU_WIRELESS_PSK_MAX || p < min_len) {
        return 0;
    }
    *slen = (uint8_t)s;
    *plen = (uint8_t)p;
    return 1;
}

/**
 * @brief Load the stored profile from the durable record
 *
 * Returns 0 unless the magic, the checksum and the field bounds all hold,
 * so a fresh, torn or corrupted record is rejected.
 *
 * @param unused    Unused; kept for ABI symmetry
 * @param out_ssid  Destination SSID buffer (33 bytes, NUL-padded), or NULL
 * @param out_psk   Destination passphrase buffer (64 bytes), or NULL
 * @param out_auth  Receives the stored auth flavor (0=WPA2, 1=WPA3), or NULL
 * @return Stored SSID length on success, 0 if no valid record exists
 */
static int wifi_cred_load(uint8_t unused, char out_ssid[33],
                          char out_psk[64], uint8_t *out_auth)
{
    uint8_t s;
    uint8_t p;
    uint8_t i;

    (void)unused;
    if (wifi_cred_nvm.magic != WIFI_CRED_MAGIC ||
        wifi_cred_xor(&wifi_cred_nvm) != wifi_cred_nvm.xor_check ||
        !wifi_profile_lengths(wifi_cred_nvm.ssid, wifi_cred_nvm.psk,
                              wifi_cred_nvm.auth_flavor, &s, &p) ||
        wifi_cred_nvm.ssid_len != s) {
        return 0;
    }
    if (out_ssid != NULL) {
        for (i = 0U; i < sizeof wifi_cred_nvm.ssid; i++) {
            out_ssid[i] = (i < s) ? wifi_cred_nvm.ssid[i] : '\0';
        }
    }
    if (out_psk != NULL) {
        for (i = 0U; i < sizeof wifi_cred_nvm.psk; i++) {
            out_psk[i] = (i < p) ? wifi_cred_nvm.psk[i] : '\0';
        }
    }
    if (out_auth != NULL) {
        *out_auth = wifi_cred_nvm.auth_flavor;
    }
    return s;
}

/**
 * @brief Zero a buffer that held a passphrase.
 *
 * Writes through a volatile pointer so the compiler keeps the stores.
 *
 * @param ptr     Buffer to clear
 * @param length  Bytes to clear
 */
static void wifi_clear_bytes(void *ptr, size_t length)
{
    volatile uint8_t *p = ptr;

    while (length--) {
        *p++ = 0U;
    }
}

/**
 * @brief Store a profile that joined as the durable record
 *
 * The magic is cleared before the body is written and set again last, so a
 * write cut part-way leaves a record that loads as empty.
 *
 * @param ssid_len  Length of @p ssid; must match the string
 * @param ssid      Null-terminated SSID
 * @param psk       Null-terminated passphrase
 * @param auth      Auth flavor to record (0=WPA2-PSK, 1=WPA3-SAE)
 * @return TIKU_DRV_OK, TIKU_DRV_ERR_INVALID for a bad profile, or
 *         TIKU_DRV_ERR_IO when the NVM flush fails
 */
static int wifi_cred_save(uint8_t ssid_len, const char *ssid,
                          const char *psk, uint8_t auth)
{
    wifi_cred_persist_t next;
    uint8_t  s;
    uint8_t  p;
    uint8_t  i;
    uint16_t saved;

    if (!wifi_profile_lengths(ssid, psk, auth, &s, &p) || s != ssid_len) {
        wifi_store_result = TIKU_DRV_ERR_INVALID;
        return wifi_store_result;
    }
    wifi_clear_bytes(&next, sizeof next);
    next.magic = WIFI_CRED_MAGIC;
    next.ssid_len = s;
    next.auth_flavor = auth;
    for (i = 0U; i < s; i++) {
        next.ssid[i] = ssid[i];
    }
    for (i = 0U; i < p; i++) {
        next.psk[i] = psk[i];
    }
    next.xor_check = wifi_cred_xor(&next);

    saved = tiku_mpu_unlock_nvm();
    wifi_cred_nvm.magic = 0UL;
    for (i = (uint8_t)sizeof next.magic; i < sizeof next; i++) {
        ((uint8_t *)&wifi_cred_nvm)[i] = ((const uint8_t *)&next)[i];
    }
    wifi_cred_nvm.magic = WIFI_CRED_MAGIC;
    wifi_store_result = (tiku_mpu_lock_nvm_status(saved) == TIKU_MEM_OK) ?
                        TIKU_DRV_OK : TIKU_DRV_ERR_IO;
    wifi_clear_bytes(&next, sizeof next);
    return wifi_store_result;
}

/**
 * @brief Erase the whole durable record.
 *
 * A logical erase: the record reads as empty afterwards, but the NVM cells
 * are not scrubbed beyond this one write.
 *
 * @return TIKU_DRV_OK, or TIKU_DRV_ERR_IO when the NVM flush fails
 */
static int wifi_cred_forget(void)
{
    uint16_t saved = tiku_mpu_unlock_nvm();

    wifi_clear_bytes(&wifi_cred_nvm, sizeof wifi_cred_nvm);
    wifi_store_result = (tiku_mpu_lock_nvm_status(saved) == TIKU_MEM_OK) ?
                        TIKU_DRV_OK : TIKU_DRV_ERR_IO;
    return wifi_store_result;
}

/**
 * @brief Describe the stored profile without its passphrase.
 *
 * @param out  Receives validity, auth flavor, SSID and the last store result
 * @return TIKU_DRV_OK, or TIKU_DRV_ERR_INVALID if @p out is NULL
 */
int tiku_wireless_saved_profile(tiku_wireless_saved_profile_t *out)
{
    if (out == NULL) {
        return TIKU_DRV_ERR_INVALID;
    }
    wifi_clear_bytes(out, sizeof *out);
    out->valid = (uint8_t)(wifi_cred_load(0U, out->ssid, NULL,
                                          &out->auth) > 0);
    out->last_store_result = wifi_store_result;
    return TIKU_DRV_OK;
}
