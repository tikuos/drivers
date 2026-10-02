/*
 * Tiku Drivers - ESP32-C61 radio: the station's supplicant
 *
 * The libraries leave WPA to a supplicant they call through a table, and
 * consult it even to scan.  This one is tikuOS's own, written from IEEE
 * 802.11-2020 12.7: it reads RSN elements, and joins open networks and
 * WPA2-PSK ones -- CCMP both ways, no PMF -- through the 4-way and group
 * key handshakes, over TikuKits' HMAC-SHA1 and AES key wrap.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arch/esp32c61/tiku_trng_arch.h>
#include <tikukits/crypto/aeskw/tiku_kits_crypto_aeskw.h>
#include <tikukits/crypto/hmac/tiku_kits_crypto_hmac_sha1.h>
#include "esp_heap.h"
#include "esp_port.h"

#define EID_RSN         48U
#define EID_VENDOR      221U
#define SUITE_BYTES     4U

/* The supplicant's own cipher bits, as the libraries' supplicant used them;
 * only the public enum leaves this file. */
#define CIPHER_NONE     (1U << 0)
#define CIPHER_TKIP     (1U << 1)
#define CIPHER_CCMP     (1U << 3)
#define CIPHER_CMAC     (1U << 5)
#define CIPHER_WEP40    (1U << 7)
#define CIPHER_WEP104   (1U << 8)
#define CIPHER_GCMP     (1U << 11)
#define CIPHER_GCMP256  (1U << 12)
#define CIPHER_GMAC     (1U << 13)
#define CIPHER_GMAC256  (1U << 14)

/* An EAPOL-Key frame (IEEE 802.1X-2020 11.3, IEEE 802.11-2020 12.7.2): the
 * 802.1X header, then the body at these offsets, then the key data. */
#define ETH_HDR         14U
#define ETHERTYPE_EAPOL 0x888EU
#define EAPOL_HDR       4U
#define EAPOL_VERSION   1U
#define EAPOL_KEY       3U
#define KEY_DESC_RSN    2U
#define K_INFO          1U
#define K_REPLAY        5U
#define K_NONCE         13U
#define K_RSC           61U
#define K_MIC           77U
#define K_DATA_LEN      93U
#define KEY_BODY        95U

#define KI_VERSION      0x0007U
#define KI_SHA1_AES     2U          /* HMAC-SHA1 MIC, AES key wrap */
#define KI_PAIRWISE     (1U << 3)
#define KI_INSTALL      (1U << 6)
#define KI_ACK          (1U << 7)
#define KI_MIC          (1U << 8)
#define KI_SECURE       (1U << 9)
#define KI_ERROR        (1U << 10)
#define KI_REQUEST      (1U << 11)
#define KI_ENCRYPTED    (1U << 12)

#define ADDR_LEN        6U
#define NONCE_LEN       32U
#define REPLAY_LEN      8U
#define MIC_LEN         16U
#define PMK_LEN         32U
#define KEY_LEN         16U         /* KCK, KEK, TK and the GTK, for CCMP */
#define PTK_KCK         0U
#define PTK_KEK         16U
#define PTK_TK          32U
#define PTK_LEN         48U
#define GTK_RSC_LEN     6U
#define KDE_GTK         1U
#define KEY_DATA_MAX    256U

typedef enum {
    WPA_IDLE,           /* no network, or an open one */
    WPA_ASSOCIATING,    /* waiting for message 1 */
    WPA_PTK_START,      /* message 2 out: waiting for message 3 */
    WPA_PTK_DONE,       /* message 4 out: the keys go in once it has left */
    WPA_DONE            /* keys in: group rekeys from here */
} wpa_state_t;

static const uint8_t oui_rsn[3] = { 0x00, 0x0F, 0xAC };    /* IEEE 802.11 */
static const uint8_t oui_wpa[3] = { 0x00, 0x50, 0xF2 };    /* WPA, vendor */

/* What the association asks for: CCMP both ways, PSK, no MFP.  The stack
 * keeps the pointer, so this is never cleared. */
static uint8_t wpa_assoc_ie[] = {
    EID_RSN, 20, 1, 0,
    0x00, 0x0F, 0xAC, 4,                /* group: CCMP */
    1, 0, 0x00, 0x0F, 0xAC, 4,          /* pairwise: CCMP */
    1, 0, 0x00, 0x0F, 0xAC, 2,          /* AKM: PSK */
    0, 0                                /* capabilities */
};

/* A group key as the AP gave it: key, index, and its receive counter. */
typedef struct {
    uint8_t key[KEY_LEN];
    uint8_t idx;
    uint8_t rsc[GTK_RSC_LEN];
} wpa_gtk_t;

/* One station, one handshake: called only from the libraries' task. */
static struct {
    uint8_t state;
    uint8_t renew;                      /* a new SNonce at the next message 1 */
    uint8_t replay_set;
    uint8_t own[ADDR_LEN];              /* SPA */
    uint8_t bssid[ADDR_LEN];            /* AA */
    uint8_t replay[REPLAY_LEN];         /* the last one a MIC proved */
    uint8_t pmk[PMK_LEN];
    uint8_t anonce[NONCE_LEN];
    uint8_t snonce[NONCE_LEN];
    uint8_t tptk[PTK_LEN];              /* from message 1, until 3 proves it */
    uint8_t ptk[PTK_LEN];
    wpa_gtk_t gtk;                      /* the one in use, or to go in */
} s;

static uint8_t wpa_pmk[PMK_LEN];        /* the driver's, for the next join */
static uint8_t wpa_pmk_set;
static uint8_t wpa_rx[EAPOL_HDR + KEY_BODY + KEY_DATA_MAX];
static uint8_t wpa_tx[ETH_HDR + EAPOL_HDR + KEY_BODY + sizeof wpa_assoc_ie];
static uint8_t wpa_kd[KEY_DATA_MAX];

/*---------------------------------------------------------------------------*/
/* The RSN and WPA elements                                                  */
/*---------------------------------------------------------------------------*/

/** @brief A cipher suite selector to a cipher bit; 0 for one unknown. */
static unsigned suite_cipher(const uint8_t *s4, const uint8_t *oui) {
    if (memcmp(s4, oui, 3) != 0) {
        return 0U;
    }
    switch (s4[3]) {
    case 0:  return CIPHER_NONE;        /* RSN: use the group cipher */
    case 1:  return CIPHER_WEP40;
    case 2:  return CIPHER_TKIP;
    case 4:  return CIPHER_CCMP;
    case 5:  return CIPHER_WEP104;
    case 6:  return CIPHER_CMAC;
    case 8:  return CIPHER_GCMP;
    case 9:  return CIPHER_GCMP256;
    case 11: return CIPHER_GMAC;
    case 12: return CIPHER_GMAC256;
    default: return 0U;
    }
}

/** @brief An AKM suite selector to a key management bit; 0 if unknown. */
static unsigned suite_akm(const uint8_t *s4, int rsn) {
    if (!rsn) {
        if (memcmp(s4, oui_wpa, 3) != 0) {
            return 0U;
        }
        return s4[3] == 1U ? WPA_KEY_MGMT_IEEE8021X :
               s4[3] == 2U ? WPA_KEY_MGMT_PSK : 0U;
    }
    if (memcmp(s4, oui_rsn, 3) != 0) {
        return 0U;
    }
    switch (s4[3]) {
    case 1:  return WPA_KEY_MGMT_IEEE8021X;
    case 2:  return WPA_KEY_MGMT_PSK;
    case 3:  return WPA_KEY_MGMT_FT_IEEE8021X;
    case 4:  return WPA_KEY_MGMT_FT_PSK;
    case 5:  return WPA_KEY_MGMT_IEEE8021X_SHA256;
    case 6:  return WPA_KEY_MGMT_PSK_SHA256;
    case 8:  return WPA_KEY_MGMT_SAE;
    case 9:  return WPA_KEY_MGMT_FT_SAE;
    case 11: return WPA_KEY_MGMT_SUITE_B;
    case 12: return WPA_KEY_MGMT_SUITE_B_192;
    case 13: return WPA_KEY_MGMT_FT_IEEE8021X_SHA384;
    case 18: return WPA_KEY_MGMT_OWE;
    case 24: return WPA_KEY_MGMT_SAE_EXT_KEY;
    default: return 0U;
    }
}

/** @brief Cipher bits to the one public value the stack reads. */
static int cipher_public(unsigned c) {
    switch (c) {
    case CIPHER_NONE:               return WIFI_CIPHER_TYPE_NONE;
    case CIPHER_WEP40:              return WIFI_CIPHER_TYPE_WEP40;
    case CIPHER_WEP104:             return WIFI_CIPHER_TYPE_WEP104;
    case CIPHER_TKIP:               return WIFI_CIPHER_TYPE_TKIP;
    case CIPHER_CCMP:               return WIFI_CIPHER_TYPE_CCMP;
    case CIPHER_CCMP | CIPHER_TKIP: return WIFI_CIPHER_TYPE_TKIP_CCMP;
    case CIPHER_CMAC:               return WIFI_CIPHER_TYPE_AES_CMAC128;
    case CIPHER_GMAC:               return WIFI_CIPHER_TYPE_AES_GMAC128;
    case CIPHER_GMAC256:            return WIFI_CIPHER_TYPE_AES_GMAC256;
    case CIPHER_GCMP:               return WIFI_CIPHER_TYPE_GCMP;
    case CIPHER_GCMP256:            return WIFI_CIPHER_TYPE_GCMP256;
    default:                        return WIFI_CIPHER_TYPE_UNKNOWN;
    }
}

/** @brief The stack's cipher, given as a bit number, to its bit. */
static unsigned cipher_bit(uint8_t n) {
    return n < 16U ? 1U << n : 0U;
}

/** @brief A two-byte little-endian count at @p p. */
static unsigned le16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/**
 * @brief Read an RSN element (ID 48) or a WPA one (vendor 221, 00-50-F2 type
 *        1), header included.  Fields an element stops before take their
 *        defaults: CCMP both ways and 802.1X for RSN, TKIP and 802.1X for
 *        WPA.  @return 0, or -1 for an element too short or of another kind
 */
static int wpa_parse(const uint8_t *ie, size_t len, wifi_wpa_ie_t *out) {
    unsigned group, pair = 0U, akm = 0U, mgmt, n;
    const uint8_t *p, *end;
    int rsn;

    memset(out, 0, sizeof *out);
    if (ie == NULL || len < 2U || (size_t)ie[1] + 2U > len) {
        return -1;
    }
    end = ie + 2 + ie[1];
    rsn = ie[0] == EID_RSN;
    if (rsn) {
        p = ie + 2;
    } else if (ie[0] == EID_VENDOR && ie[1] >= 4U &&
               memcmp(ie + 2, oui_wpa, 3) == 0 && ie[5] == 1U) {
        p = ie + 6;
    } else {
        return -1;
    }
    if (end - p < 2 || le16(p) != 1U) {             /* version 1 */
        return -1;
    }
    p += 2;
    group = rsn ? CIPHER_CCMP : CIPHER_TKIP;
    mgmt = rsn ? CIPHER_CMAC : 0U;          /* RSN's default; WPA has none */
    out->proto = rsn ? WPA_PROTO_RSN : WPA_PROTO_WPA;
    if (end - p >= (ptrdiff_t)SUITE_BYTES) {
        group = suite_cipher(p, rsn ? oui_rsn : oui_wpa);
        p += SUITE_BYTES;
    }
    if (end - p >= 2) {
        n = le16(p);
        p += 2;
        for (; n > 0U && end - p >= (ptrdiff_t)SUITE_BYTES;
             n--, p += SUITE_BYTES) {
            pair |= suite_cipher(p, rsn ? oui_rsn : oui_wpa);
        }
    } else {
        pair = group;
    }
    if (end - p >= 2) {
        n = le16(p);
        p += 2;
        for (; n > 0U && end - p >= (ptrdiff_t)SUITE_BYTES;
             n--, p += SUITE_BYTES) {
            akm |= suite_akm(p, rsn);
        }
    } else {
        akm = WPA_KEY_MGMT_IEEE8021X;
    }
    if (rsn && end - p >= 2) {
        out->capabilities = (int)le16(p);
        p += 2;
    }
    if (rsn && end - p >= 2) {
        n = le16(p);
        p += 2;
        if (n != 0U && end - p >= (ptrdiff_t)(16U * n)) {
            out->num_pmkid = n;
            out->pmkid = p;
            p += 16U * n;
        }
    }
    if (rsn && end - p >= (ptrdiff_t)SUITE_BYTES) {
        mgmt = suite_cipher(p, oui_rsn);
    }
    out->pairwise_cipher = cipher_public(pair);
    out->group_cipher = cipher_public(group);
    out->key_mgmt = (int)akm;
    out->mgmt_group_cipher = cipher_public(mgmt);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Keys                                                                      */
/*---------------------------------------------------------------------------*/

static unsigned be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | (unsigned)p[1];
}

static void put_be16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/** @brief Equal in time that does not depend on where they differ. */
static int same(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t d = 0U;

    while (n-- > 0U) {
        d |= (uint8_t)(*a++ ^ *b++);
    }
    return d == 0U;
}

/** @brief Forget every key, the PMK's copy included. */
static void wpa_clear(void) {
    memset(&s, 0, sizeof s);
}

/**
 * @brief The pairwise transient key (12.7.1.3): PRF-384 of the PMK over
 *        "Pairwise key expansion", the addresses and then the nonces, each
 *        pair smaller first; the PRF's HMAC-SHA1 rounds count from 0.
 */
static void wpa_derive_ptk(uint8_t ptk[PTK_LEN]) {
    static const char label[] = "Pairwise key expansion";
    tiku_kits_crypto_hmac_sha1_ctx_t h;
    uint8_t data[2U * ADDR_LEN + 2U * NONCE_LEN];
    uint8_t out[3U * TIKU_KITS_CRYPTO_HMAC_SHA1_SIZE];
    uint8_t i;
    int lo;

    lo = memcmp(s.own, s.bssid, ADDR_LEN) < 0;
    memcpy(data, lo ? s.own : s.bssid, ADDR_LEN);
    memcpy(data + ADDR_LEN, lo ? s.bssid : s.own, ADDR_LEN);
    lo = memcmp(s.snonce, s.anonce, NONCE_LEN) < 0;
    memcpy(data + 2U * ADDR_LEN, lo ? s.snonce : s.anonce, NONCE_LEN);
    memcpy(data + 2U * ADDR_LEN + NONCE_LEN, lo ? s.anonce : s.snonce,
           NONCE_LEN);
    for (i = 0U; i < 3U; i++) {
        (void)tiku_kits_crypto_hmac_sha1_init(&h, s.pmk, PMK_LEN);
        /* The label with its NUL: the PRF's zero octet after it. */
        (void)tiku_kits_crypto_hmac_sha1_update(&h, (const uint8_t *)label,
                                                sizeof label);
        (void)tiku_kits_crypto_hmac_sha1_update(&h, data, sizeof data);
        (void)tiku_kits_crypto_hmac_sha1_update(&h, &i, 1U);
        (void)tiku_kits_crypto_hmac_sha1_final(&h,
            out + i * TIKU_KITS_CRYPTO_HMAC_SHA1_SIZE);
    }
    memcpy(ptk, out, PTK_LEN);
    memset(out, 0, sizeof out);
    memset(&h, 0, sizeof h);
}

/** @brief The MIC of an EAPOL frame (802.1X header on) under @p kck, its
 *         MIC field zero: HMAC-SHA1 cut to 16 bytes. */
static void wpa_mic(const uint8_t *kck, const uint8_t *f, size_t n,
                    uint8_t mic[MIC_LEN]) {
    uint8_t full[TIKU_KITS_CRYPTO_HMAC_SHA1_SIZE];

    (void)tiku_kits_crypto_hmac_sha1(kck, KEY_LEN, f, n, full);
    memcpy(mic, full, MIC_LEN);
    memset(full, 0, sizeof full);
}

/** @brief Whether the received frame @p f carries the right MIC under
 *         @p kck.  Zeroes the field as it checks. */
static int wpa_mic_ok(uint8_t *f, size_t n, const uint8_t *kck) {
    uint8_t want[MIC_LEN], got[MIC_LEN];
    uint8_t *field = f + EAPOL_HDR + K_MIC;

    memcpy(want, field, MIC_LEN);
    memset(field, 0, MIC_LEN);
    wpa_mic(kck, f, n, got);
    return same(got, want, MIC_LEN);
}

/**
 * @brief Send an EAPOL-Key frame to the AP: the key information bits, the
 *        replay counter echoed, an optional nonce and key data, and the MIC
 *        under @p kck.  Key length is 0 in every frame a station sends.
 */
static int wpa_send(unsigned info, const uint8_t *replay,
                    const uint8_t *nonce, const uint8_t *data, size_t dlen,
                    const uint8_t *kck) {
    uint8_t *e = wpa_tx + ETH_HDR;
    uint8_t *k = e + EAPOL_HDR;
    size_t n = EAPOL_HDR + KEY_BODY + dlen;

    memset(wpa_tx, 0, ETH_HDR + n);
    memcpy(wpa_tx, s.bssid, ADDR_LEN);
    memcpy(wpa_tx + ADDR_LEN, s.own, ADDR_LEN);
    put_be16(wpa_tx + 2U * ADDR_LEN, ETHERTYPE_EAPOL);
    e[0] = EAPOL_VERSION;
    e[1] = EAPOL_KEY;
    put_be16(e + 2, (unsigned)(KEY_BODY + dlen));
    k[0] = KEY_DESC_RSN;
    put_be16(k + K_INFO, info | KI_SHA1_AES);
    memcpy(k + K_REPLAY, replay, REPLAY_LEN);
    if (nonce != NULL) {
        memcpy(k + K_NONCE, nonce, NONCE_LEN);
    }
    put_be16(k + K_DATA_LEN, (unsigned)dlen);
    if (dlen != 0U) {
        memcpy(k + KEY_BODY, data, dlen);
    }
    wpa_mic(kck, e, n, k + K_MIC);
    return esp_wifi_internal_tx(WIFI_IF_STA, wpa_tx, (uint16_t)(ETH_HDR + n));
}

/**
 * @brief Unwrap the key data of a frame under the KEK and find what it
 *        carries: the AP's RSN element (if @p rsn) and the GTK KDE, read
 *        into @p out.  Padding (0xDD then zeros) ends it.  @return 0, or -1
 */
static int wpa_key_data(const uint8_t *k, size_t dlen, const uint8_t *kek,
                        const uint8_t **rsn, wpa_gtk_t *out) {
    const uint8_t *p = wpa_kd, *gtk = NULL;
    size_t n;

    if (dlen < 24U || (dlen % 8U) != 0U || dlen - 8U > sizeof wpa_kd ||
        tiku_kits_crypto_aeskw_unwrap(kek, KEY_LEN, k + KEY_BODY, dlen,
                                      wpa_kd) != TIKU_KITS_CRYPTO_OK) {
        return -1;
    }
    n = dlen - 8U;
    if (rsn != NULL) {
        *rsn = NULL;
    }
    while (n >= 2U && !(p[0] == EID_VENDOR && p[1] == 0U)) {
        size_t len = (size_t)p[1] + 2U;

        if (len > n) {
            return -1;
        }
        if (p[0] == EID_RSN && rsn != NULL) {
            *rsn = p;
        } else if (p[0] == EID_VENDOR && len >= 8U &&
                   memcmp(p + 2, oui_rsn, 3) == 0 && p[5] == KDE_GTK) {
            gtk = p;
        }
        p += len;
        n -= len;
    }
    if (gtk == NULL || gtk[1] != 6U + KEY_LEN) {
        return -1;                          /* no GTK, or not a CCMP one */
    }
    memcpy(out->key, gtk + 8, KEY_LEN);
    out->idx = gtk[6] & 0x03U;
    memcpy(out->rsc, k + K_RSC, GTK_RSC_LEN);
    return 0;
}

/** @brief Whether the AP's RSN element in message 3 still offers what was
 *         joined: CCMP for group and pairwise, PSK. */
static int wpa_rsn_ok(const uint8_t *ie) {
    wifi_wpa_ie_t d;

    return ie != NULL && wpa_parse(ie, (size_t)ie[1] + 2U, &d) == 0 &&
           d.group_cipher == WIFI_CIPHER_TYPE_CCMP &&
           (d.pairwise_cipher == WIFI_CIPHER_TYPE_CCMP ||
            d.pairwise_cipher == WIFI_CIPHER_TYPE_TKIP_CCMP) &&
           ((unsigned)d.key_mgmt & WPA_KEY_MGMT_PSK) != 0U;
}

static void wpa_install_gtk(void) {
    (void)esp_wifi_set_sta_key_internal(WIFI_WPA_ALG_CCMP, s.bssid,
        s.gtk.idx, 0, s.gtk.rsc, GTK_RSC_LEN, s.gtk.key, KEY_LEN,
        KEY_FLAG_GROUP | KEY_FLAG_RX);
}

/*---------------------------------------------------------------------------*/
/* The handshakes                                                            */
/*---------------------------------------------------------------------------*/

/** @brief Message 1: the AP's nonce.  Answer with ours and our RSN
 *         element, the MIC under a PTK that message 3 will prove. */
static void wpa_msg1(const uint8_t *k) {
    if (s.renew) {
        if (tiku_trng_arch_read_bytes(s.snonce, NONCE_LEN) != 0) {
            return;
        }
        s.renew = 0U;
    }
    memcpy(s.anonce, k + K_NONCE, NONCE_LEN);
    wpa_derive_ptk(s.tptk);
    if (wpa_send(KI_PAIRWISE | KI_MIC, k + K_REPLAY, s.snonce, wpa_assoc_ie,
                 sizeof wpa_assoc_ie, s.tptk + PTK_KCK) == 0) {
        s.state = WPA_PTK_START;
    }
}

/**
 * @brief Message 3: the PTK proved, the GTK in it.  Answer with message 4,
 *        its Secure bit message 3's; the keys go in once it has left.  A
 *        repeat, after the keys are in, is answered again, installing nothing.
 */
static void wpa_msg3(uint8_t *f, size_t n, unsigned info, size_t dlen) {
    const uint8_t *k = f + EAPOL_HDR;
    const uint8_t *ptk = s.state == WPA_DONE ? s.ptk : s.tptk;
    const uint8_t *rsn;
    wpa_gtk_t gtk;

    if (s.state < WPA_PTK_START || (info & KI_ENCRYPTED) == 0U ||
        !same(k + K_NONCE, s.anonce, NONCE_LEN) ||
        !wpa_mic_ok(f, n, ptk + PTK_KCK) ||
        wpa_key_data(k, dlen, ptk + PTK_KEK, &rsn, &gtk) != 0 ||
        !wpa_rsn_ok(rsn)) {
        return;
    }
    memcpy(s.replay, k + K_REPLAY, REPLAY_LEN);
    s.replay_set = 1U;
    if (s.state != WPA_DONE) {
        memcpy(s.ptk, s.tptk, PTK_LEN);
        s.gtk = gtk;
        s.state = WPA_PTK_DONE;
    }
    memset(&gtk, 0, sizeof gtk);
    (void)wpa_send(KI_PAIRWISE | KI_MIC | (info & KI_SECURE), k + K_REPLAY,
                   NULL, NULL, 0U, s.ptk + PTK_KCK);
}

/** @brief Group message 1: a new GTK.  It goes in (unless it is the one in
 *         use), then message 2 answers. */
static void wpa_group1(uint8_t *f, size_t n, unsigned info, size_t dlen) {
    const uint8_t *k = f + EAPOL_HDR;
    wpa_gtk_t gtk;

    if (s.state != WPA_DONE || (info & KI_ENCRYPTED) == 0U ||
        !wpa_mic_ok(f, n, s.ptk + PTK_KCK) ||
        wpa_key_data(k, dlen, s.ptk + PTK_KEK, NULL, &gtk) != 0) {
        return;
    }
    memcpy(s.replay, k + K_REPLAY, REPLAY_LEN);
    if (gtk.idx != s.gtk.idx || !same(gtk.key, s.gtk.key, KEY_LEN)) {
        s.gtk = gtk;
        wpa_install_gtk();
    }
    memset(&gtk, 0, sizeof gtk);
    (void)wpa_send(KI_MIC | KI_SECURE, k + K_REPLAY, NULL, NULL, 0U,
                   s.ptk + PTK_KCK);
    (void)esp_wifi_auth_done_internal();    /* as after every rekey */
}

/** @brief An EAPOL frame from the stack, at its 802.1X header. */
static int wpa_rx_eapol(uint8_t *src, uint8_t *buf, uint32_t len) {
    const uint8_t *k = wpa_rx + EAPOL_HDR;
    size_t body, dlen;
    unsigned info;

    if (s.state == WPA_IDLE || src == NULL || buf == NULL ||
        memcmp(src, s.bssid, ADDR_LEN) != 0 ||
        len < EAPOL_HDR + KEY_BODY || buf[1] != EAPOL_KEY) {
        return 0;
    }
    body = be16(buf + 2);
    if (body < KEY_BODY || body > len - EAPOL_HDR ||
        EAPOL_HDR + body > sizeof wpa_rx) {
        return 0;
    }
    memcpy(wpa_rx, buf, EAPOL_HDR + body);
    info = be16(k + K_INFO);
    dlen = be16(k + K_DATA_LEN);
    if (k[0] != KEY_DESC_RSN || KEY_BODY + dlen > body ||
        (info & KI_VERSION) != KI_SHA1_AES ||
        (info & (KI_ERROR | KI_REQUEST)) != 0U || (info & KI_ACK) == 0U ||
        (s.replay_set && memcmp(k + K_REPLAY, s.replay, REPLAY_LEN) <= 0)) {
        return 0;
    }
    if ((info & KI_PAIRWISE) == 0U) {
        if ((info & (KI_MIC | KI_SECURE)) == (KI_MIC | KI_SECURE)) {
            wpa_group1(wpa_rx, EAPOL_HDR + body, info, dlen);
        }
    } else if ((info & KI_MIC) == 0U) {
        wpa_msg1(k);
    } else if ((info & KI_INSTALL) != 0U) {
        wpa_msg3(wpa_rx, EAPOL_HDR + body, info, dlen);
    }
    memset(wpa_kd, 0, sizeof wpa_kd);       /* the unwrapped keys */
    return 0;
}

/** @brief An EAPOL frame has left.  Message 4 out: the PTK and GTK go in,
 *         and the stack hears the handshake is done. */
static void wpa_tx_done(uint8_t *eapol, size_t len, bool failed) {
    static uint8_t rsc0[REPLAY_LEN];
    unsigned info;

    if (s.state != WPA_PTK_DONE || eapol == NULL ||
        len < EAPOL_HDR + KEY_BODY || eapol[1] != EAPOL_KEY) {
        return;
    }
    info = be16(eapol + EAPOL_HDR + K_INFO);
    if ((info & KI_PAIRWISE) == 0U ||
        be16(eapol + EAPOL_HDR + K_DATA_LEN) != 0U || failed) {
        return;                     /* message 2, or 4 lost: 3 comes again */
    }
    (void)esp_wifi_set_sta_key_internal(WIFI_WPA_ALG_CCMP, s.bssid, 0, 1,
        rsc0, sizeof rsc0, s.ptk + PTK_TK, KEY_LEN,
        KEY_FLAG_PAIRWISE | KEY_FLAG_RX | KEY_FLAG_TX);
    wpa_install_gtk();
    s.state = WPA_DONE;
    s.renew = 1U;
    memset(s.tptk, 0, sizeof s.tptk);
    (void)esp_wifi_auth_done_internal();
}

/*---------------------------------------------------------------------------*/
/* The station's calls                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Ready a WPA2-PSK join: the network must be CCMP both ways and PSK.
 *        A new profile's PMK comes from the driver; otherwise the stack's
 *        copy stands.  @return 0, or -1 for one this supplicant cannot join
 */
static int wpa_begin(const uint8_t *bssid) {
    unsigned pair = cipher_bit(esp_wifi_sta_get_pairwise_cipher_internal());
    unsigned group = cipher_bit(esp_wifi_sta_get_group_cipher_internal());
    uint8_t *pmk = esp_wifi_sta_get_ap_info_prof_pmk_internal();

    if (esp_wifi_sta_get_prof_authmode_internal() != WPA2_AUTH_PSK ||
        pair != CIPHER_CCMP || group != CIPHER_CCMP || pmk == NULL) {
        ESPW_PRINTF("join: only WPA2-PSK with CCMP (auth %u, ciphers "
                    "0x%x/0x%x)\n",
                    (unsigned)esp_wifi_sta_get_prof_authmode_internal(),
                    pair, group);
        return -1;
    }
    if (esp_wifi_sta_get_reset_nvs_pmk_internal() != 0U) {
        if (!wpa_pmk_set) {
            return -1;
        }
        memcpy(pmk, wpa_pmk, PMK_LEN);
        (void)esp_wifi_sta_update_ap_info_internal();
        (void)esp_wifi_sta_set_reset_nvs_pmk_internal(0U);
    }
    memcpy(s.pmk, pmk, PMK_LEN);
    memcpy(s.bssid, bssid, ADDR_LEN);
    (void)esp_wifi_get_macaddr_internal(WIFI_IF_STA, s.own);
    s.renew = 1U;
    s.state = WPA_ASSOCIATING;
    (void)esp_wifi_set_appie_internal(WIFI_APPIE_RSN, wpa_assoc_ie,
                                      sizeof wpa_assoc_ie, 1U);
    return 0;
}

static int wpa_connect(uint8_t *bssid) {
    uint8_t auth = esp_wifi_sta_get_prof_authmode_internal();

    wpa_clear();
    if (bssid == NULL) {
        return -1;
    }
    if (auth == NONE_AUTH) {
        (void)esp_wifi_unset_appie_internal(WIFI_APPIE_RSN);
        (void)esp_wifi_unset_appie_internal(WIFI_APPIE_WPA);
    } else if (!esp_wifi_sta_prof_is_rsn_internal() || wpa_begin(bssid) != 0) {
        wpa_clear();
        return -1;
    }
    return esp_wifi_sta_connect_internal(bssid);
}

static void wpa_connected(uint8_t *bssid) {
    (void)bssid;
}

static void wpa_disconnected(uint8_t reason) {
    (void)reason;
    wpa_clear();
}

static bool wpa_in_4way(void) {
    return s.state == WPA_PTK_START || s.state == WPA_PTK_DONE;
}

static bool wpa_init(void) {
    wpa_clear();
    return esp_wifi_register_eapol_txdonecb_internal(wpa_tx_done) == 0;
}

static bool wpa_deinit(void) {
    (void)esp_wifi_register_eapol_txdonecb_internal(NULL);
    wpa_clear();
    return true;
}

static int wpa_mic_failure(uint16_t is_unicast) {
    (void)is_unicast;                   /* TKIP's alone: never joined here */
    return 0;
}

static void wpa_nothing(void) {
}

void espw_wpa_set_pmk(const uint8_t *pmk) {
    if (pmk != NULL) {
        memcpy(wpa_pmk, pmk, PMK_LEN);
    } else {
        memset(wpa_pmk, 0, sizeof wpa_pmk);
    }
    wpa_pmk_set = pmk != NULL;
}

int espw_wpa_register(void) {
    wpa_funcs_t *cb = espw_calloc(1U, sizeof *cb);

    if (cb == NULL) {
        return -1;
    }
    cb->wpa_sta_init = wpa_init;
    cb->wpa_sta_deinit = wpa_deinit;
    cb->wpa_sta_connect = wpa_connect;
    cb->wpa_sta_connected_cb = wpa_connected;
    cb->wpa_sta_disconnected_cb = wpa_disconnected;
    cb->wpa_sta_rx_eapol = wpa_rx_eapol;
    cb->wpa_sta_in_4way_handshake = wpa_in_4way;
    cb->wpa_parse_wpa_ie = wpa_parse;
    cb->wpa_parse_wpa_ie_scan_only = wpa_parse;
    cb->wpa_michael_mic_failure = wpa_mic_failure;
    cb->wpa_config_done = wpa_nothing;
    cb->wpa_sta_clear_curr_pmksa = wpa_nothing;
    cb->wpa_config_reload = wpa_nothing;
    if (esp_wifi_register_wpa_cb_internal(cb) != 0) {
        espw_free(cb);
        return -1;
    }
    return 0;
}

void espw_wpa_unregister(void) {
    (void)esp_wifi_unregister_wpa_cb_internal();     /* frees the table */
    wpa_clear();
}
