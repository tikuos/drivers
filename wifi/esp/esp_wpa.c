/*
 * Tiku Drivers - ESP32-C61 radio: what the stack asks of a supplicant
 *
 * The libraries leave WPA to a supplicant they call through a table, and
 * consult it even to scan.  This one is tikuOS's own, written from IEEE
 * 802.11: it reads the RSN and WPA elements of what a scan finds, and
 * answers the station's other calls as a supplicant with nothing to do yet.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

static const uint8_t oui_rsn[3] = { 0x00, 0x0F, 0xAC };    /* IEEE 802.11 */
static const uint8_t oui_wpa[3] = { 0x00, 0x50, 0xF2 };    /* WPA, vendor */

/** @brief A cipher suite selector to a cipher bit; 0 for one unknown. */
static unsigned suite_cipher(const uint8_t *s, const uint8_t *oui) {
    if (memcmp(s, oui, 3) != 0) {
        return 0U;
    }
    switch (s[3]) {
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
static unsigned suite_akm(const uint8_t *s, int rsn) {
    if (!rsn) {
        if (memcmp(s, oui_wpa, 3) != 0) {
            return 0U;
        }
        return s[3] == 1U ? WPA_KEY_MGMT_IEEE8021X :
               s[3] == 2U ? WPA_KEY_MGMT_PSK : 0U;
    }
    if (memcmp(s, oui_rsn, 3) != 0) {
        return 0U;
    }
    switch (s[3]) {
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
/* The station's calls: nothing to do until the handshake is here          */
/*---------------------------------------------------------------------------*/

static bool wpa_yes(void) {
    return true;
}

static bool wpa_no(void) {
    return false;
}

static void wpa_nothing(void) {
}

static int wpa_connect(uint8_t *bssid) {
    (void)bssid;
    return 0;
}

static void wpa_connected(uint8_t *bssid) {
    (void)bssid;
}

static void wpa_disconnected(uint8_t reason) {
    (void)reason;
}

static int wpa_rx_eapol(uint8_t *src, uint8_t *buf, uint32_t len) {
    (void)src;
    (void)buf;
    (void)len;
    return 0;
}

static int wpa_mic_failure(uint16_t is_unicast) {
    (void)is_unicast;
    return 0;
}

int espw_wpa_register(void) {
    wpa_funcs_t *cb = espw_calloc(1U, sizeof *cb);

    if (cb == NULL) {
        return -1;
    }
    cb->wpa_sta_init = wpa_yes;
    cb->wpa_sta_deinit = wpa_yes;
    cb->wpa_sta_connect = wpa_connect;
    cb->wpa_sta_connected_cb = wpa_connected;
    cb->wpa_sta_disconnected_cb = wpa_disconnected;
    cb->wpa_sta_rx_eapol = wpa_rx_eapol;
    cb->wpa_sta_in_4way_handshake = wpa_no;
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
}
