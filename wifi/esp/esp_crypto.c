/*
 * Tiku Drivers - ESP32-C61 radio: the crypto the libraries call themselves
 *
 * The stack takes a table of crypto at init and calls into it on its own
 * paths: PBKDF2, SHA-256 and AES for the soft-AP and the extras, CMAC, GMAC
 * and software CCMP for protected management frames.  Each entry is here,
 * over TikuKits; the PMF ones fail, as this port joins with PMF off.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <tikukits/crypto/aes128/tiku_kits_crypto_aes128.h>
#include <tikukits/crypto/aeskw/tiku_kits_crypto_aeskw.h>
#include <tikukits/crypto/pbkdf2/tiku_kits_crypto_pbkdf2.h>
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>
#include "esp_port.h"

#define AES_BLOCK       16U
#define SHA256_BLOCK    64U
#define SHA256_DIGEST   32U

static int crypto_sha256_vector(size_t num, const uint8_t *addr[],
                                const size_t *len, uint8_t *out) {
    tiku_kits_crypto_sha256_ctx_t c;
    size_t i;

    (void)tiku_kits_crypto_sha256_init(&c);
    for (i = 0U; i < num; i++) {
        (void)tiku_kits_crypto_sha256_update(&c, addr[i], len[i]);
    }
    return tiku_kits_crypto_sha256_final(&c, out) == TIKU_KITS_CRYPTO_OK ?
           0 : -1;
}

/** @brief HMAC-SHA256 (RFC 2104) over pieces; a key over a block is hashed
 *         first. */
static int crypto_hmac_sha256_vector(const unsigned char *key, int key_len,
                                     int num, const unsigned char *addr[],
                                     const int *len, unsigned char *mac) {
    tiku_kits_crypto_sha256_ctx_t c;
    uint8_t pad[SHA256_BLOCK], inner[SHA256_DIGEST];
    int i;

    if (key == NULL || key_len < 0 || num < 0 || mac == NULL) {
        return -1;
    }
    memset(pad, 0, sizeof pad);
    if ((size_t)key_len > SHA256_BLOCK) {
        (void)tiku_kits_crypto_sha256_hash(key, (size_t)key_len, pad);
    } else {
        memcpy(pad, key, (size_t)key_len);
    }
    for (i = 0; i < (int)SHA256_BLOCK; i++) {
        pad[i] ^= 0x36U;
    }
    (void)tiku_kits_crypto_sha256_init(&c);
    (void)tiku_kits_crypto_sha256_update(&c, pad, sizeof pad);
    for (i = 0; i < num; i++) {
        (void)tiku_kits_crypto_sha256_update(&c, addr[i], (size_t)len[i]);
    }
    (void)tiku_kits_crypto_sha256_final(&c, inner);
    for (i = 0; i < (int)SHA256_BLOCK; i++) {
        pad[i] ^= 0x36U ^ 0x5CU;
    }
    (void)tiku_kits_crypto_sha256_init(&c);
    (void)tiku_kits_crypto_sha256_update(&c, pad, sizeof pad);
    (void)tiku_kits_crypto_sha256_update(&c, inner, sizeof inner);
    (void)tiku_kits_crypto_sha256_final(&c, mac);
    memset(pad, 0, sizeof pad);
    memset(inner, 0, sizeof inner);
    memset(&c, 0, sizeof c);
    return 0;
}

static int crypto_pbkdf2_sha1(const char *passphrase, const char *ssid,
                              unsigned int ssid_len, int iterations,
                              unsigned char *buf, unsigned int buflen) {
    if (passphrase == NULL || iterations <= 0) {
        return -1;
    }
    return tiku_kits_crypto_pbkdf2_hmac_sha1((const uint8_t *)passphrase,
               strlen(passphrase), (const uint8_t *)ssid, ssid_len,
               (uint32_t)iterations, buf, buflen) == TIKU_KITS_CRYPTO_OK ?
           0 : -1;
}

/** @brief AES-128-CBC in place over whole blocks, either way. */
static int crypto_aes_cbc(const unsigned char *key, const unsigned char *iv,
                          unsigned char *data, int data_len, int encrypt) {
    tiku_kits_crypto_aes128_ctx_t c;
    uint8_t chain[AES_BLOCK], next[AES_BLOCK];
    unsigned i;
    int off;

    if (key == NULL || iv == NULL || data == NULL || data_len < 0 ||
        (data_len % (int)AES_BLOCK) != 0) {
        return -1;
    }
    (void)tiku_kits_crypto_aes128_init(&c, key);
    memcpy(chain, iv, AES_BLOCK);
    for (off = 0; off < data_len; off += (int)AES_BLOCK) {
        uint8_t *b = data + off;

        if (encrypt) {
            for (i = 0U; i < AES_BLOCK; i++) {
                b[i] ^= chain[i];
            }
            (void)tiku_kits_crypto_aes128_encrypt(&c, b, b);
            memcpy(chain, b, AES_BLOCK);
        } else {
            memcpy(next, b, AES_BLOCK);
            (void)tiku_kits_crypto_aes128_decrypt(&c, b, b);
            for (i = 0U; i < AES_BLOCK; i++) {
                b[i] ^= chain[i];
            }
            memcpy(chain, next, AES_BLOCK);
        }
    }
    memset(&c, 0, sizeof c);
    return 0;
}

static int crypto_aes_128_encrypt(const unsigned char *key,
                                  const unsigned char *iv,
                                  unsigned char *data, int data_len) {
    return crypto_aes_cbc(key, iv, data, data_len, 1);
}

static int crypto_aes_128_decrypt(const unsigned char *key,
                                  const unsigned char *iv,
                                  unsigned char *data, int data_len) {
    return crypto_aes_cbc(key, iv, data, data_len, 0);
}

static int crypto_aes_wrap(const unsigned char *kek, size_t kek_len, int n,
                           const unsigned char *plain,
                           unsigned char *cipher) {
    return n > 0 && tiku_kits_crypto_aeskw_wrap(kek, kek_len, plain,
                        (size_t)n * 8U, cipher) == TIKU_KITS_CRYPTO_OK ?
           0 : -1;
}

static int crypto_aes_unwrap(const unsigned char *kek, size_t kek_len, int n,
                             const unsigned char *cipher,
                             unsigned char *plain) {
    return n > 0 && tiku_kits_crypto_aeskw_unwrap(kek, kek_len, cipher,
                        (size_t)(n + 1) * 8U, plain) == TIKU_KITS_CRYPTO_OK ?
           0 : -1;
}

/* Protected management frames: not offered, so each says it failed. */

static int crypto_no_omac1(const uint8_t *key, const uint8_t *data,
                           size_t data_len, uint8_t *mic) {
    (void)key;
    (void)data;
    (void)data_len;
    (void)mic;
    return -1;
}

static uint8_t *crypto_no_ccmp_decrypt(const uint8_t *tk, const uint8_t *hdr,
                                       const uint8_t *data, size_t data_len,
                                       size_t *decrypted_len, bool espnow) {
    (void)tk;
    (void)hdr;
    (void)data;
    (void)data_len;
    (void)decrypted_len;
    (void)espnow;
    return NULL;
}

static uint8_t *crypto_no_ccmp_encrypt(const uint8_t *tk, uint8_t *frame,
                                       size_t len, size_t hdrlen, uint8_t *pn,
                                       int keyid, size_t *encrypted_len) {
    (void)tk;
    (void)frame;
    (void)len;
    (void)hdrlen;
    (void)pn;
    (void)keyid;
    (void)encrypted_len;
    return NULL;
}

static int crypto_no_gmac(const uint8_t *key, size_t keylen,
                          const uint8_t *iv, size_t iv_len,
                          const uint8_t *aad, size_t aad_len, uint8_t *mic) {
    (void)key;
    (void)keylen;
    (void)iv;
    (void)iv_len;
    (void)aad;
    (void)aad_len;
    (void)mic;
    return -1;
}

void espw_crypto_table(wpa_crypto_funcs_t *t) {
    t->size = sizeof *t;
    t->version = ESP_WIFI_CRYPTO_VERSION;
    t->hmac_sha256_vector = crypto_hmac_sha256_vector;
    t->pbkdf2_sha1 = crypto_pbkdf2_sha1;
    t->aes_128_encrypt = crypto_aes_128_encrypt;
    t->aes_128_decrypt = crypto_aes_128_decrypt;
    t->omac1_aes_128 = crypto_no_omac1;
    t->ccmp_decrypt = crypto_no_ccmp_decrypt;
    t->ccmp_encrypt = crypto_no_ccmp_encrypt;
    t->aes_gmac = crypto_no_gmac;
    t->sha256_vector = crypto_sha256_vector;
    t->aes_wrap = crypto_aes_wrap;
    t->aes_unwrap = crypto_aes_unwrap;
}
