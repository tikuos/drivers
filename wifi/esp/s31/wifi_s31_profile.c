/*
 * Tiku Drivers
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * wifi_s31_profile.c - Passphrase-encrypted S31 station or AP credentials.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_s31_profile.h"
#include <arch/esp32s31/tiku_settings_arch.h>
#include <arch/esp32s31/tiku_trng_arch.h>
#include <interfaces/wireless/tiku_wireless_ap.h>
#include <kernel/process/tiku_process.h>
#include <kernel/threads/tiku_thread.h>
#include <arch/esp32s31/tiku_runtime_arch.h>
#include <hal/tiku_watchdog_hal.h>
#include <tikukits/crypto/pbkdf2/tiku_kits_crypto_pbkdf2.h>
#include <tikukits/crypto/gcm/tiku_kits_crypto_gcm.h>
#include <string.h>
typedef struct {
    uint8_t mode, auth, channel, reserved;
    char ssid[33], psk[64];
} profile_t;
typedef struct {
    uint8_t version[4], salt[16], nonce[12];
    uint8_t ciphertext[sizeof(profile_t)], tag[16];
} vault_t;
static profile_t cached;
static unsigned busy, unlocked;
static int store_result;
/** @brief Erase key and plaintext temporaries through volatile stores. */
static void wipe(void *data, size_t length)
{ volatile uint8_t *p = data; while (length--) *p++ = 0; }
/** @brief Keep the watchdog and other processes running while deriving a vault key. */
static void progress(void *owner)
{
    tiku_watchdog_kick();
    (void)tiku_process_run_except(owner);
    tiku_current_process = owner;
}
/** @brief Derive a 256-bit vault key using PBKDF2-HMAC-SHA256. */
static int derive(const char *pass, const uint8_t salt[16], uint8_t key[32])
{
    size_t n = pass ? strnlen(pass, 129) : 0;
    if (n < 12 || n > 128) return TIKU_DRV_ERR_INVALID;
    return tiku_kits_crypto_pbkdf2_sha256((const uint8_t *)pass, n, salt, 16,
        200000, key, progress, TIKU_THIS()) ? TIKU_DRV_ERR_IO : 0;
}
/** @brief Read a versioned envelope without exposing its plaintext. */
static int read_vault(vault_t *vault)
{
    size_t n;
    int rc = tiku_s31_settings_read(vault, sizeof *vault, &n);
    if (rc) return rc;
    return n == sizeof *vault && !memcmp(vault->version, "SWF1", 4) ? 0 : -1;
}
/** @brief Validate decrypted bounds before passing credentials to the radio. */
static int valid(const profile_t *p)
{
    size_t sl = strnlen(p->ssid, sizeof p->ssid), pl = strnlen(p->psk, sizeof p->psk);
    return (p->mode == 1 || p->mode == 2) && !p->reserved &&
        sl && sl <= 32 && pl <= 63 && p->auth <= TIKU_WIRELESS_AUTH_WPA3_SAE &&
        (p->auth == TIKU_WIRELESS_AUTH_WPA3_SAE ? pl != 0 : (!pl || pl >= 8)) &&
        (p->mode == 1 ? p->channel == 0 : p->channel >= 1 && p->channel <= 11);
}
int espw_s31_profile_busy(void) { return busy != 0; }
void espw_s31_profile_remember(const char *ssid, const char *psk, unsigned auth,
                               unsigned ap, uint8_t channel)
{
    wipe(&cached, sizeof cached);
    cached.mode = ap ? 2 : 1; cached.auth = auth; cached.channel = channel;
    memcpy(cached.ssid, ssid, strlen(ssid) + 1);
    memcpy(cached.psk, psk, strlen(psk) + 1);
    unlocked = 0;
}
int tiku_wireless_save_profile(void) { return TIKU_DRV_ERR_INVALID; }
int tiku_wireless_save_encrypted(const char *pass)
{
    if (tiku_s31_in_trap || !tiku_thread_in_kernel() || busy || !valid(&cached) || !pass || strnlen(pass, 129) < 12 || strnlen(pass, 129) > 128)
        return TIKU_DRV_ERR_INVALID;
    tiku_wireless_status_t station;
    tiku_wireless_ap_status_t ap;
    if (cached.mode == 1 ? tiku_wireless_status(&station) ||
        station.link_state != TIKU_WIRELESS_LINK_JOINED || station.joined_ssid_len != strlen(cached.ssid) ||
        memcmp(station.joined_ssid, cached.ssid, station.joined_ssid_len) :
        tiku_wireless_ap_status(&ap) || !ap.active) return TIKU_DRV_ERR_INVALID;
    busy = 1;
    vault_t next = {0}; uint8_t key[32] = {0};
    tiku_kits_crypto_gcm_ctx_t cipher = {0};
    memcpy(next.version, "SWF1", 4);
    int rc = tiku_trng_arch_read_bytes(next.salt, sizeof next.salt) ||
             tiku_trng_arch_read_bytes(next.nonce, sizeof next.nonce) ? TIKU_DRV_ERR_IO : 0;
    if (!rc) rc = espw_s31_profile_power(0);
    if (!rc) rc = derive(pass, next.salt, key);
    if (!rc && (tiku_kits_crypto_gcm_init256(&cipher, key) ||
        tiku_kits_crypto_gcm_encrypt(&cipher, next.nonce, (const uint8_t *)&next, 32,
            (const uint8_t *)&cached, sizeof cached, next.ciphertext, next.tag))) rc = TIKU_DRV_ERR_IO;
    if (!rc && tiku_s31_settings_write(&next, sizeof next)) rc = TIKU_DRV_ERR_IO;
    wipe(key, sizeof key); wipe(&cipher, sizeof cipher); wipe(&next, sizeof next);
    store_result = rc; unlocked = !rc; busy = 0;
    return rc;
}
int tiku_wireless_unlock_profile(const char *pass)
{
    if (tiku_s31_in_trap || !tiku_thread_in_kernel() || busy) return TIKU_DRV_ERR_TIMEOUT;
    tiku_wireless_status_t station;
    if (tiku_wireless_status(&station) || station.up) return TIKU_DRV_ERR_INVALID;
    busy = 1;
    vault_t vault = {0}; profile_t plain = {0}; uint8_t key[32] = {0};
    tiku_kits_crypto_gcm_ctx_t cipher = {0};
    int rc = read_vault(&vault) ? TIKU_DRV_ERR_INVALID : derive(pass, vault.salt, key);
    if (!rc && (tiku_kits_crypto_gcm_init256(&cipher, key) ||
        tiku_kits_crypto_gcm_decrypt(&cipher, vault.nonce, (const uint8_t *)&vault, 32,
            vault.ciphertext, sizeof plain, vault.tag, (uint8_t *)&plain) || !valid(&plain)))
        rc = TIKU_DRV_ERR_INVALID;
    if (!rc) rc = espw_s31_profile_power(1);
    busy = 0;
    if (!rc) rc = plain.mode == 1 ? tiku_wireless_connect_auth(plain.ssid, plain.psk, plain.auth) :
        tiku_wireless_ap_start_auth(plain.ssid, plain.psk, plain.channel, plain.auth);
    if (!rc) unlocked = 1;
    else { wipe(&cached, sizeof cached); unlocked = 0; }
    wipe(key, sizeof key); wipe(&cipher, sizeof cipher);
    wipe(&plain, sizeof plain); wipe(&vault, sizeof vault);
    return rc;
}
int espw_s31_profile_forget(void)
{
    if (tiku_s31_in_trap || !tiku_thread_in_kernel() || busy) return TIKU_DRV_ERR_TIMEOUT;
    busy = 1;
    int rc = espw_s31_profile_power(0);
    if (!rc && tiku_s31_settings_write(NULL, 0)) rc = TIKU_DRV_ERR_IO;
    wipe(&cached, sizeof cached); unlocked = 0; busy = 0; store_result = rc;
    return rc;
}
int tiku_wireless_saved_profile(tiku_wireless_saved_profile_t *out)
{
    if (tiku_s31_in_trap || !tiku_thread_in_kernel() || !out || busy) return TIKU_DRV_ERR_INVALID;
    vault_t vault;
    memset(out, 0, sizeof *out);
    int rc = read_vault(&vault);
    out->valid = !rc;
    out->last_store_result = rc < 0 ? TIKU_DRV_ERR_IO : store_result;
    if (!rc && unlocked) {
        memcpy(out->ssid, cached.ssid, sizeof out->ssid);
        out->access_point = cached.mode == 2; out->auth = cached.auth; out->channel = cached.channel;
    }
    wipe(&vault, sizeof vault);
    return 0;
}
