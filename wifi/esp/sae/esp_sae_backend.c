/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp_sae_backend.c - Hostap crypto API over Mbed TLS P-256 and TikuKits SHA-256.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_sae_engine.h"
#include "common.h"
#include "crypto/crypto.h"
#include "crypto/dh_groups.h"
#include "includes.h"
#include "utils/wpabuf.h"
#include <mbedtls/bignum.h>
#include <mbedtls/ecp.h>
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>
#define B(x) ((mbedtls_mpi *)(x))
#define P(x) ((mbedtls_ecp_point *)(x))
int os_get_random(unsigned char *p, size_t n)
{
    return espw_sae_random(p, n);
}
static int rng(void *unused, unsigned char *p, size_t n)
{
    (void)unused;
    return os_get_random(p, n);
}
void *os_zalloc(size_t n)
{
    return espw_calloc(1, n);
}
void forced_memzero(void *p, size_t n)
{
    volatile u8 *q = p;
    while (n--)
        *q++ = 0;
}
void bin_clear_free(void *p, size_t n)
{
    if (p) {
        forced_memzero(p, n);
        espw_free(p);
    }
}
int os_memcmp_const(const void *a, const void *b, size_t n)
{
    const u8 *x = a, *y = b;
    unsigned d = 0;
    while (n--)
        d |= *x++ ^ *y++;
    return d;
}
const struct dh_group *dh_groups_get(int n)
{
    (void)n;
    return NULL;
}
int sha256_vector(size_t n, const u8 *p[], const size_t len[], u8 *out)
{
    tiku_kits_crypto_sha256_ctx_t c;
    tiku_kits_crypto_sha256_init(&c);
    for (size_t i = 0; i < n; i++)
        tiku_kits_crypto_sha256_update(&c, p[i], len[i]);
    return tiku_kits_crypto_sha256_final(&c, out);
}
struct crypto_bignum *crypto_bignum_init(void)
{
    mbedtls_mpi *p = espw_calloc(1, sizeof *p);
    if (p)
        mbedtls_mpi_init(p);
    return (void *)p;
}
void crypto_bignum_deinit(struct crypto_bignum *p, int clear)
{
    (void)clear;
    if (p) {
        mbedtls_mpi_free(B(p));
        espw_free(p);
    }
}
struct crypto_bignum *crypto_bignum_init_set(const u8 *data, size_t n)
{
    struct crypto_bignum *p = crypto_bignum_init();
    if (p && mbedtls_mpi_read_binary(B(p), data, n)) {
        crypto_bignum_deinit(p, 1);
        p = NULL;
    }
    return p;
}
struct crypto_bignum *crypto_bignum_init_uint(unsigned n)
{
    struct crypto_bignum *p = crypto_bignum_init();
    if (p && mbedtls_mpi_lset(B(p), n)) {
        crypto_bignum_deinit(p, 1);
        p = NULL;
    }
    return p;
}
int crypto_bignum_to_bin(const struct crypto_bignum *p, u8 *out, size_t size, size_t pad)
{
    size_t n = mbedtls_mpi_size(B(p));
    if (n < pad)
        n = pad;
    return n > size || mbedtls_mpi_write_binary(B(p), out, n) ? -1 : (int)n;
}
int crypto_bignum_rand(struct crypto_bignum *p, const struct crypto_bignum *m)
{
    return mbedtls_mpi_random(B(p), 0, B(m), rng, NULL);
}
int crypto_bignum_add(const struct crypto_bignum *a, const struct crypto_bignum *b,
                      struct crypto_bignum *c)
{
    return mbedtls_mpi_add_mpi(B(c), B(a), B(b));
}
int crypto_bignum_sub(const struct crypto_bignum *a, const struct crypto_bignum *b,
                      struct crypto_bignum *c)
{
    return mbedtls_mpi_sub_mpi(B(c), B(a), B(b));
}
int crypto_bignum_mod(const struct crypto_bignum *a, const struct crypto_bignum *b,
                      struct crypto_bignum *c)
{
    return mbedtls_mpi_mod_mpi(B(c), B(a), B(b));
}
int crypto_bignum_div(const struct crypto_bignum *a, const struct crypto_bignum *b,
                      struct crypto_bignum *c)
{
    return mbedtls_mpi_div_mpi(B(c), NULL, B(a), B(b));
}
int crypto_bignum_inverse(const struct crypto_bignum *a, const struct crypto_bignum *b,
                          struct crypto_bignum *c)
{
    return mbedtls_mpi_inv_mod(B(c), B(a), B(b));
}
int crypto_bignum_exptmod(const struct crypto_bignum *a, const struct crypto_bignum *b,
                          const struct crypto_bignum *c, struct crypto_bignum *d)
{
    mbedtls_mpi t;
    mbedtls_mpi_init(&t);
    int r = mbedtls_mpi_exp_mod(&t, B(a), B(b), B(c), NULL);
    if (!r)
        r = mbedtls_mpi_copy(B(d), &t);
    mbedtls_mpi_free(&t);
    return r;
}
int crypto_bignum_addmod(const struct crypto_bignum *a, const struct crypto_bignum *b,
                         const struct crypto_bignum *c, struct crypto_bignum *d)
{
    mbedtls_mpi t;
    mbedtls_mpi_init(&t);
    int r = mbedtls_mpi_add_mpi(&t, B(a), B(b));
    if (!r)
        r = mbedtls_mpi_mod_mpi(B(d), &t, B(c));
    mbedtls_mpi_free(&t);
    return r;
}
int crypto_bignum_mulmod(const struct crypto_bignum *a, const struct crypto_bignum *b,
                         const struct crypto_bignum *c, struct crypto_bignum *d)
{
    mbedtls_mpi t;
    mbedtls_mpi_init(&t);
    int r = mbedtls_mpi_mul_mpi(&t, B(a), B(b));
    if (!r)
        r = mbedtls_mpi_mod_mpi(B(d), &t, B(c));
    mbedtls_mpi_free(&t);
    return r;
}
int crypto_bignum_sqrmod(const struct crypto_bignum *a, const struct crypto_bignum *b,
                         struct crypto_bignum *c)
{
    return crypto_bignum_mulmod(a, a, b, c);
}
int crypto_bignum_rshift(const struct crypto_bignum *a, int n, struct crypto_bignum *b)
{
    int r = mbedtls_mpi_copy(B(b), B(a));
    return r ? r : mbedtls_mpi_shift_r(B(b), n);
}
int crypto_bignum_cmp(const struct crypto_bignum *a, const struct crypto_bignum *b)
{
    return mbedtls_mpi_cmp_mpi(B(a), B(b));
}
int crypto_bignum_is_zero(const struct crypto_bignum *a)
{
    return !mbedtls_mpi_cmp_int(B(a), 0);
}
int crypto_bignum_is_one(const struct crypto_bignum *a)
{
    return !mbedtls_mpi_cmp_int(B(a), 1);
}
int crypto_bignum_is_odd(const struct crypto_bignum *a)
{
    return mbedtls_mpi_get_bit(B(a), 0);
}
int crypto_bignum_legendre(const struct crypto_bignum *a, const struct crypto_bignum *p)
{
    mbedtls_mpi e, r;
    mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&r);
    int rc = mbedtls_mpi_sub_int(&e, B(p), 1);
    if (!rc)
        rc = mbedtls_mpi_shift_r(&e, 1);
    if (!rc)
        rc = mbedtls_mpi_exp_mod(&r, B(a), &e, B(p), NULL);
    int out = rc ? -2 : !mbedtls_mpi_cmp_int(&r, 0) ? 0 : !mbedtls_mpi_cmp_int(&r, 1) ? 1 : -1;
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&r);
    return out;
}
struct crypto_ec {
    mbedtls_ecp_group g;
    mbedtls_mpi a;
};
struct crypto_ec *crypto_ec_init(int group)
{
    if (group != 19)
        return NULL;
    struct crypto_ec *e = espw_calloc(1, sizeof *e);
    if (!e)
        return NULL;
    mbedtls_ecp_group_init(&e->g);
    mbedtls_mpi_init(&e->a);
    if (mbedtls_ecp_group_load(&e->g, MBEDTLS_ECP_DP_SECP256R1) ||
        mbedtls_mpi_sub_int(&e->a, &e->g.P, 3)) {
        crypto_ec_deinit(e);
        return NULL;
    }
    return e;
}
void crypto_ec_deinit(struct crypto_ec *e)
{
    if (e) {
        mbedtls_mpi_free(&e->a);
        mbedtls_ecp_group_free(&e->g);
        espw_free(e);
    }
}
size_t crypto_ec_prime_len(struct crypto_ec *e)
{
    return mbedtls_mpi_size(&e->g.P);
}
size_t crypto_ec_prime_len_bits(struct crypto_ec *e)
{
    return mbedtls_mpi_bitlen(&e->g.P);
}
size_t crypto_ec_order_len(struct crypto_ec *e)
{
    return mbedtls_mpi_size(&e->g.N);
}
const struct crypto_bignum *crypto_ec_get_prime(struct crypto_ec *e)
{
    return (void *)&e->g.P;
}
const struct crypto_bignum *crypto_ec_get_order(struct crypto_ec *e)
{
    return (void *)&e->g.N;
}
const struct crypto_bignum *crypto_ec_get_a(struct crypto_ec *e)
{
    return (void *)&e->a;
}
const struct crypto_bignum *crypto_ec_get_b(struct crypto_ec *e)
{
    return (void *)&e->g.B;
}
struct crypto_ec_point *crypto_ec_point_init(struct crypto_ec *e)
{
    (void)e;
    mbedtls_ecp_point *p = espw_calloc(1, sizeof *p);
    if (p)
        mbedtls_ecp_point_init(p);
    return (void *)p;
}
void crypto_ec_point_deinit(struct crypto_ec_point *p, int clear)
{
    (void)clear;
    if (p) {
        mbedtls_ecp_point_free(P(p));
        espw_free(p);
    }
}
int crypto_ec_point_to_bin(struct crypto_ec *e, const struct crypto_ec_point *p, u8 *x, u8 *y)
{
    size_t n = crypto_ec_prime_len(e);
    return (x && mbedtls_mpi_write_binary(&P(p)->X, x, n)) ||
                   (y && mbedtls_mpi_write_binary(&P(p)->Y, y, n))
               ? -1
               : 0;
}
struct crypto_ec_point *crypto_ec_point_from_bin(struct crypto_ec *e, const u8 *p)
{
    u8 b[65];
    b[0] = 4;
    memcpy(b + 1, p, 64);
    struct crypto_ec_point *q = crypto_ec_point_init(e);
    if (q && (mbedtls_ecp_point_read_binary(&e->g, P(q), b, 65) ||
              mbedtls_ecp_check_pubkey(&e->g, P(q)))) {
        crypto_ec_point_deinit(q, 1);
        q = NULL;
    }
    return q;
}
int crypto_ec_point_mul(struct crypto_ec *e, const struct crypto_ec_point *p,
                        const struct crypto_bignum *b, struct crypto_ec_point *r)
{
    return mbedtls_ecp_mul(&e->g, P(r), B(b), P(p), rng, NULL);
}
int crypto_ec_point_add(struct crypto_ec *e, const struct crypto_ec_point *a,
                        const struct crypto_ec_point *b, struct crypto_ec_point *r)
{
    mbedtls_mpi one;
    mbedtls_mpi_init(&one);
    int rc = mbedtls_mpi_lset(&one, 1);
    if (!rc)
        rc = mbedtls_ecp_muladd(&e->g, P(r), &one, P(a), &one, P(b));
    mbedtls_mpi_free(&one);
    return rc;
}
int crypto_ec_point_invert(struct crypto_ec *e, struct crypto_ec_point *p)
{
    return mbedtls_mpi_cmp_int(&P(p)->Y, 0) ? mbedtls_mpi_sub_mpi(&P(p)->Y, &e->g.P, &P(p)->Y) : 0;
}
int crypto_ec_point_is_at_infinity(struct crypto_ec *e, const struct crypto_ec_point *p)
{
    (void)e;
    return mbedtls_ecp_is_zero(P(p));
}
int crypto_ec_point_is_on_curve(struct crypto_ec *e, const struct crypto_ec_point *p)
{
    return !mbedtls_ecp_check_pubkey(&e->g, P(p));
}
int crypto_ec_point_cmp(const struct crypto_ec *e, const struct crypto_ec_point *a,
                        const struct crypto_ec_point *b)
{
    (void)e;
    return mbedtls_ecp_point_cmp(P(a), P(b));
}
struct crypto_bignum *crypto_ec_point_compute_y_sqr(struct crypto_ec *e,
                                                    const struct crypto_bignum *x)
{
    struct crypto_bignum *y = crypto_bignum_init(), *t = crypto_bignum_init();
    const struct crypto_bignum *p = (void *)&e->g.P;
    int rc = !y || !t;
    if (!rc)
        rc = crypto_bignum_mulmod(x, x, p, y);
    if (!rc)
        rc = crypto_bignum_mulmod(y, x, p, y);
    if (!rc)
        rc = crypto_bignum_mulmod((void *)&e->a, x, p, t);
    if (!rc)
        rc = crypto_bignum_addmod(y, t, p, y);
    if (!rc)
        rc = crypto_bignum_addmod(y, (void *)&e->g.B, p, y);
    crypto_bignum_deinit(t, 1);
    if (rc) {
        crypto_bignum_deinit(y, 1);
        y = NULL;
    }
    return y;
}
size_t int_array_len(const int *p)
{
    size_t n = 0;
    if (p)
        while (p[n])
            n++;
    return n;
}
u32 wpa_akm_to_suite(int akm)
{
    return akm == (1 << 10) ? 0x000fac08u : 0;
}
