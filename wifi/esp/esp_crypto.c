/*
 * Tiku Drivers - ESP32-C61 radio: the crypto the libraries call themselves
 *
 * The stack receives a crypto table at initialization. TikuKits supplies
 * PBKDF2, SHA-256, AES and CMAC. Protected management frames use CCMP and
 * AES-CMAC; the GMAC callback rejects requests.
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
#include "esp_heap.h"
#include <tikukits/crypto/ccm/tiku_kits_crypto_ccm.h>

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

/** @brief Erase temporary key material through volatile stores. */
static void crypto_wipe(void *p, size_t n)
{
    volatile uint8_t *q = p;
    while (n--) *q++ = 0;
}

/** @brief Double a CMAC subkey in GF(2^128), with reduction polynomial 0x87. */
static void cmac_double(uint8_t k[16])
{
    uint8_t carry = k[0] >> 7;
    for (unsigned i = 0; i < 15; i++) k[i] = (k[i] << 1) | (k[i + 1] >> 7);
    k[15] = (k[15] << 1) ^ (0x87u & (0u - carry));
}

int espw_cmac(const uint8_t *key, const uint8_t *data, size_t len, uint8_t *mic)
{
    tiku_kits_crypto_aes128_ctx_t aes;
    uint8_t subkey[16] = {0}, block[16] = {0};
    if (!key || (!data && len) || !mic) return -1;
    (void)tiku_kits_crypto_aes128_init(&aes, key);
    (void)tiku_kits_crypto_aes128_encrypt(&aes, subkey, subkey);
    cmac_double(subkey);
    while (len > 16) {
        for (unsigned i = 0; i < 16; i++) block[i] ^= data[i];
        (void)tiku_kits_crypto_aes128_encrypt(&aes, block, block);
        data += 16; len -= 16;
    }
    if (len < 16) {
        cmac_double(subkey);
        block[len] ^= 0x80;
    }
    for (unsigned i = 0; i < len; i++) block[i] ^= data[i];
    for (unsigned i = 0; i < 16; i++) block[i] ^= subkey[i];
    (void)tiku_kits_crypto_aes128_encrypt(&aes, block, mic);
    crypto_wipe(&aes, sizeof aes);
    crypto_wipe(subkey, sizeof subkey);
    crypto_wipe(block, sizeof block);
    return 0;
}

void espw_ptk_sha256(const uint8_t pmk[32], const uint8_t data[76], uint8_t ptk[48])
{
    static const uint8_t label[] = "Pairwise key expansion";
    uint8_t count[2] = {1,0}, bits[2] = {0x80,1}, digest[32];
    const uint8_t *addr[] = {count, label, data, bits};
    const int lengths[] = {2, sizeof label - 1, 76, 2};
    (void)crypto_hmac_sha256_vector(pmk, 32, 4, addr, lengths, digest);
    memcpy(ptk, digest, 32);
    count[0] = 2;
    (void)crypto_hmac_sha256_vector(pmk, 32, 4, addr, lengths, digest);
    memcpy(ptk + 32, digest, 16);
    crypto_wipe(digest, sizeof digest);
}

/** @brief Accept unicast version-zero management headers with three addresses. */
static int pmf_header(const uint8_t *h)
{
    return h && !(h[0] & 0x0f) && !(h[1] & 0x83) && !(h[4] & 1);
}

/** @brief Form the management CCMP nonce and masked authentication data. */
static void pmf_aad(const uint8_t *h, const uint8_t *ccmp,
                    uint8_t aad[22], uint8_t nonce[13])
{
    aad[0] = h[0]; aad[1] = (h[1] & ~0x38u) | 0x40u;
    memcpy(aad + 2, h + 4, 18);
    aad[20] = h[22] & 15; aad[21] = 0;
    nonce[0] = 0x10;
    memcpy(nonce + 1, h + 10, 6);
    nonce[7] = ccmp[7]; nonce[8] = ccmp[6]; nonce[9] = ccmp[5];
    nonce[10] = ccmp[4]; nonce[11] = ccmp[1]; nonce[12] = ccmp[0];
}

/** @brief Authenticate management CCMP; the vendor checks the peer's receive PN. */
static uint8_t *crypto_ccmp_decrypt(const uint8_t *tk, const uint8_t *hdr,
                                  const uint8_t *data, size_t len,
                                  size_t *decrypted_len, bool espnow)
{
    uint8_t aad[22], nonce[13], tag[8], diff = 0;
    if (!decrypted_len) return NULL;
    *decrypted_len = 0;
    if (!tk || !pmf_header(hdr) || !(hdr[1] & 0x40) || !data || espnow ||
        len < 16 || len - 16 > 65535 || data[2] || data[3] != 0x20)
        return NULL;
    size_t n = len - 16;
    uint8_t *out = espw_malloc(n ? n : 1);
    if (!out) return NULL;
    pmf_aad(hdr, data, aad, nonce);
    int rc = tiku_kits_crypto_ccm_star(1, tk, nonce, aad, sizeof aad,
                                      data + 8, n, 8, out, tag);
    for (unsigned i = 0; i < 8; i++) diff |= tag[i] ^ data[8 + n + i];
    crypto_wipe(tag, sizeof tag);
    if (rc || diff) {
        crypto_wipe(out, n); espw_free(out); return NULL;
    }
    *decrypted_len = n;
    return out;
}

/** @brief Encrypt management payload stored after the vendor's eight-byte CCMP gap. */
static uint8_t *crypto_ccmp_encrypt(const uint8_t *tk, uint8_t *frame,
                                  size_t len, size_t hdrlen, uint8_t *pn,
                                  int keyid, size_t *encrypted_len)
{
    uint8_t aad[22], nonce[13];
    if (!encrypted_len) return NULL;
    *encrypted_len = 0;
    if (!tk || !frame || !pn || hdrlen != 24 || len < hdrlen ||
        len - hdrlen > 65535 || !pmf_header(frame) || keyid != 0)
        return NULL;
    size_t n = len - hdrlen;
    uint8_t *out = espw_malloc(len + 16);
    if (!out) return NULL;
    memcpy(out, frame, hdrlen); out[1] |= 0x40;
    uint8_t *c = out + hdrlen;
    c[0] = pn[5]; c[1] = pn[4]; c[2] = 0; c[3] = 0x20 | (keyid << 6);
    c[4] = pn[3]; c[5] = pn[2]; c[6] = pn[1]; c[7] = pn[0];
    pmf_aad(out, c, aad, nonce);
    if (tiku_kits_crypto_ccm_star(0, tk, nonce, aad, sizeof aad,
                                frame + hdrlen + 8, n, 8, c + 8, c + 8 + n)) {
        crypto_wipe(out, len + 16); espw_free(out); return NULL;
    }
    *encrypted_len = len + 16;
    return out;
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
    t->omac1_aes_128 = espw_cmac;
    t->ccmp_decrypt = crypto_ccmp_decrypt;
    t->ccmp_encrypt = crypto_ccmp_encrypt;
    t->aes_gmac = crypto_no_gmac;
    t->sha256_vector = crypto_sha256_vector;
    t->aes_wrap = crypto_aes_wrap;
    t->aes_unwrap = crypto_aes_unwrap;
}
