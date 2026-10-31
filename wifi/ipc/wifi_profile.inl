/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_profile.inl - Passphrase-encrypted P4 station and AP profiles.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/memory/tiku_mem.h>
#include <hal/tiku_ipc_epoch_hal.h>
#include <tikukits/crypto/gcm/tiku_kits_crypto_gcm.h>
#include <tikukits/crypto/pbkdf2/tiku_kits_crypto_pbkdf2.h>
#define PROFILE_ROUNDS 200000u
typedef struct {
    uint8_t mode, dual, station_auth, ap_auth, channel, reserved[3];
    char station_ssid[33], station_psk[64], ap_ssid[33], ap_psk[64];
} radio_profile_t;
typedef struct {
    uint8_t version[4], salt[16], nonce[12];
    uint8_t ciphertext[sizeof(radio_profile_t)], tag[16];
} radio_vault_t;
static TIKU_DURABLE radio_vault_t stored_profile;
TIKU_PERSIST_CELL(profile_cell, stored_profile, 0x57495032u, NULL, 0);
static int store_result;
static uint8_t profile_busy, profile_unlocked;
/** @brief Recognize the versioned envelope; authentication occurs during unlock. */
static int profile_valid(void)
{
    return tiku_persist_cell_valid(&profile_cell) &&
           !memcmp(stored_profile.version, (uint8_t[]){'W','I','F',2}, 4);
}
/** @brief Keep saved credentials locked after boot. */
static void profile_boot(void)
{
    (void)tiku_persist_cell_init(&profile_cell);
    profile_unlocked = 0;
}
/** @brief Allow radio ownership renewals and service work during password derivation. */
static void profile_progress(void *caller)
{
    tiku_watchdog_kick();
    (void)tiku_sched_run_once_except(caller);
}
/** @brief Derive the vault key without retaining the passphrase or derived key. */
static int profile_key(const char *pass, const uint8_t salt[16], uint8_t key[32])
{
    size_t n = pass ? strnlen(pass, 129) : 0;
    if (n < 12 || n > 128) return TIKU_DRV_ERR_INVALID;
    return tiku_kits_crypto_pbkdf2_sha256((const uint8_t *)pass, n, salt, 16,
        PROFILE_ROUNDS, key, profile_progress, TIKU_THIS()) ? TIKU_DRV_ERR_IO : TIKU_DRV_OK;
}
/** @brief Fill a fresh salt and nonce from hardware-backed IPC entropy. */
static int profile_random(uint8_t *p, size_t n)
{
    while (n) {
        uint64_t value;
        if (tiku_ipc_epoch_fresh(&value)) return -1;
        size_t part = n < 8 ? n : 8;
        memcpy(p, &value, part); p += part; n -= part;
        tiku_radio_wipe(&value, sizeof value);
    }
    return 0;
}
/** @brief Erase the saved envelope; failed durable writes remain visible to callers. */
static int profile_erase(void)
{
    if (profile_busy) return TIKU_DRV_ERR_TIMEOUT;
    radio_vault_t empty = {0};
    store_result = tiku_persist_cell_commit_status(&profile_cell, &empty, sizeof empty) == TIKU_MEM_OK
        ? TIKU_DRV_OK : TIKU_DRV_ERR_IO;
    if (!store_result) profile_unlocked = 0;
    return store_result;
}
int tiku_wireless_save_profile(void) { return TIKU_DRV_ERR_INVALID; }
int tiku_wireless_save_encrypted(const char *pass)
{
    if (profile_busy || !profile || !want_power || !cached.up || desired_op ||
        active_op == TIKU_RADIO_CONNECT || active_op == TIKU_RADIO_SOFTAP_START ||
        ((profile & 1) && cached.link_state != TIKU_WIRELESS_LINK_JOINED) ||
        ((profile & 2) && !cached_ap.active)) return TIKU_DRV_ERR_INVALID;
    profile_busy = 1;
    radio_profile_t plain = {0}; radio_vault_t next = {0}; uint8_t key[32] = {0};
    tiku_kits_crypto_gcm_ctx_t cipher;
    plain.mode = profile; plain.dual = want_dual;
    if (profile & 1) {
        plain.station_auth = saved_auth;
        memcpy(plain.station_ssid, saved_ssid, 33); memcpy(plain.station_psk, saved_psk, 64);
    }
    if (profile & 2) {
        plain.ap_auth = want_dual ? saved_ap_auth : saved_auth;
        plain.channel = want_dual ? saved_ap_channel : saved_channel;
        memcpy(plain.ap_ssid, want_dual ? saved_ap_ssid : saved_ssid, 33);
        memcpy(plain.ap_psk, want_dual ? saved_ap_psk : saved_psk, 64);
    }
    memcpy(next.version, (uint8_t[]){'W','I','F',2}, 4);
    int rc = profile_random(next.salt, sizeof next.salt) || profile_random(next.nonce, sizeof next.nonce)
        ? TIKU_DRV_ERR_IO : profile_key(pass, next.salt, key);
    if (!rc && (tiku_kits_crypto_gcm_init256(&cipher, key) ||
        tiku_kits_crypto_gcm_encrypt(&cipher, next.nonce, (const uint8_t *)&next, 32,
            (const uint8_t *)&plain, sizeof plain, next.ciphertext, next.tag))) rc = TIKU_DRV_ERR_IO;
    if (!rc && tiku_persist_cell_commit_status(&profile_cell, &next, sizeof next) != TIKU_MEM_OK)
        rc = TIKU_DRV_ERR_IO;
    tiku_radio_wipe(key, sizeof key); tiku_radio_wipe(&cipher, sizeof cipher);
    tiku_radio_wipe(&plain, sizeof plain); tiku_radio_wipe(&next, sizeof next);
    profile_busy = 0; store_result = rc;
    if (!rc) profile_unlocked = 1;
    return rc;
}
/** @brief Validate decrypted string bounds and negotiated authentication. */
static int profile_pair(const char *ssid, const char *psk, uint8_t auth)
{
    size_t sl = strnlen(ssid, 33), pl = strnlen(psk, 64);
    return sl && sl < 33 && pl < 64 && auth <= TIKU_WIRELESS_AUTH_WPA3_SAE &&
        (auth == TIKU_WIRELESS_AUTH_WPA3_SAE ? pl != 0 : (!pl || pl >= 8));
}
int tiku_wireless_unlock_profile(const char *pass)
{
    if (profile_busy || !profile_valid() || profile || desired_op || active_op == TIKU_RADIO_CONNECT ||
        active_op == TIKU_RADIO_SOFTAP_START || cached_ap.active ||
        cached.link_state != TIKU_WIRELESS_LINK_IDLE) return TIKU_DRV_ERR_INVALID;
    profile_busy = 1;
    radio_profile_t plain = {0}; radio_vault_t vault = stored_profile; uint8_t key[32] = {0};
    tiku_kits_crypto_gcm_ctx_t cipher;
    int rc = profile_key(pass, vault.salt, key);
    if (!rc && (tiku_kits_crypto_gcm_init256(&cipher, key) ||
        tiku_kits_crypto_gcm_decrypt(&cipher, vault.nonce, (const uint8_t *)&vault, 32,
            vault.ciphertext, sizeof plain, vault.tag, (uint8_t *)&plain))) rc = TIKU_DRV_ERR_INVALID;
    if (!rc && (!plain.mode || plain.mode > 3 || plain.dual > 1 ||
        (plain.mode == 3 && !plain.dual) || memcmp(plain.reserved, (uint8_t[3]){0}, 3) ||
        ((plain.mode & 1) && !profile_pair(plain.station_ssid, plain.station_psk, plain.station_auth)) ||
        ((plain.mode & 2) && (!profile_pair(plain.ap_ssid, plain.ap_psk, plain.ap_auth) ||
                             plain.channel < 1 || plain.channel > 11)))) rc = TIKU_DRV_ERR_INVALID;
    if (!rc) {
        want_dual = plain.dual;
        if (plain.mode & 1) {
            memcpy(saved_ssid, plain.station_ssid, 33); memcpy(saved_psk, plain.station_psk, 64);
            saved_auth = plain.station_auth;
        }
        if (plain.mode & 2) {
            memcpy(want_dual ? saved_ap_ssid : saved_ssid, plain.ap_ssid, 33);
            memcpy(want_dual ? saved_ap_psk : saved_psk, plain.ap_psk, 64);
            saved_ap_auth = saved_auth = plain.ap_auth;
            saved_ap_channel = saved_channel = plain.channel;
            if (plain.mode & 1) saved_auth = plain.station_auth;
        }
        profile = plain.mode; want_power = 1; profile_unlocked = 1;
    }
    tiku_radio_wipe(key, sizeof key); tiku_radio_wipe(&cipher, sizeof cipher);
    tiku_radio_wipe(&plain, sizeof plain); tiku_radio_wipe(&vault, sizeof vault);
    profile_busy = 0;
    return rc;
}
int tiku_wireless_saved_profile(tiku_wireless_saved_profile_t *out)
{
    if (!out) return TIKU_DRV_ERR_INVALID;
    memset(out, 0, sizeof *out);
    out->valid = profile_valid();
    if (out->valid && profile_unlocked) {
        memcpy(out->ssid, profile == 2 && want_dual ? saved_ap_ssid : saved_ssid, sizeof out->ssid);
        out->access_point = profile == 2;
        out->dual = want_dual;
        out->auth = profile == 2 && want_dual ? saved_ap_auth : saved_auth;
        out->channel = profile & 2 ? (want_dual ? saved_ap_channel : saved_channel) : 0;
    }
    out->last_store_result = store_result;
    return TIKU_DRV_OK;
}
