/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * esp_ap.c - Four-client WPA2/SAE CCMP access-point authenticator.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_ap.h"
#include "esp_port.h"
#include "esp_heap.h"
#if TIKU_ESP_SAE_ENABLE
#include "esp_sae.h"
#endif
#include <string.h>
#include <tikukits/crypto/hmac/tiku_kits_crypto_hmac_sha1.h>
#include <tikukits/crypto/aeskw/tiku_kits_crypto_aeskw.h>

/* The vendor invokes these callbacks on its Wi-Fi task. */
typedef struct {
    void **sm;
    uint8_t *mac, *ie, *rsnxe;
    bool *pmf;
    uint8_t *cipher, *selection, *owe;
    int subtype;
    uint16_t rsnxe_len;
    uint8_t ie_len, owe_len;
} ap_join_t;
typedef struct {
    wifi_interface_t ifx;
    uint8_t subtype;
    uint32_t data_len;
    uint8_t da[6], data[2];
} ap_assoc_t;
typedef struct { int (*fn)(void *); void *arg; uint32_t size; } ap_ipc_t;
extern int esp_wifi_set_ap_key_internal(int alg, const uint8_t *mac, int index,
                                      uint8_t *key, size_t length);
extern bool esp_wifi_wpa_ptk_init_done_internal(uint8_t *mac);
extern int esp_wifi_ap_deauth_internal(uint8_t *mac, uint32_t reason);
extern int esp_wifi_send_mgmt_frm_internal(const void *request);
extern int esp_wifi_set_appie_internal(uint8_t type, uint8_t *ie, uint16_t len, uint8_t flag);
extern int esp_wifi_unset_appie_internal(uint8_t type);
extern int esp_wifi_ipc_internal(ap_ipc_t *config, bool sync);

#if defined(__riscv) && __riscv_xlen == 32
_Static_assert(sizeof(ap_join_t) == 40, "AP join ABI");
_Static_assert(offsetof(ap_join_t, ie_len) == 38, "AP IE length ABI");
_Static_assert(offsetof(ap_assoc_t, data) == 18, "AP association ABI");
#endif

static const uint8_t ap_ie[] = {
    48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0
};
static const uint8_t sae_ie[] = {
    48,26,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,8,0xc0,0,
    0,0,0,15,172,6,244,1,0x20
};
static const uint8_t broadcast[6] = {255,255,255,255,255,255};
#ifndef ESPW_AP_REKEY_SECONDS
#define ESPW_AP_REKEY_SECONDS 3600u
#endif
#if ESPW_AP_REKEY_SECONDS < 1 || ESPW_AP_REKEY_SECONDS > 86400
#error AP group renewal interval must be 1 through 86400 seconds
#endif
typedef struct {
    uint8_t pmk[32], ptk[48], anonce[32], snonce[32];
    uint8_t mac[6], peer_ie[48], peer_ie_len, state, attempts;
    uint32_t replay;
    tiku_clock_time_t deadline;
} ap_peer_t;
static ap_peer_t peers[ESPW_AP_MAX_CLIENTS];
static struct {
    uint8_t pmk[32], gtk[16], next_gtk[16], igtk[16], next_igtk[16];
    uint8_t gtk_index, igtk_index, sae, own[6], ie[sizeof sae_ie], ie_len;
    uint8_t enabled, secured, renewing, group_used;
    tiku_clock_time_t deadline;
} ap __attribute__((aligned(4)));
/* Peer states: 0 vacant, 1/2 pairwise, 3 authorized, 4/6 group, 5 waiting for group. */
/** @brief Find a peer by MAC; vacancies have state zero. */
static ap_peer_t *find_peer(const uint8_t *mac)
{
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++)
        if (peers[i].state && !memcmp(peers[i].mac, mac, 6)) return &peers[i];
    return NULL;
}
uint8_t espw_ap_clients(void)
{
    uint8_t n = 0;
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++)
        n += peers[i].state == 3 || peers[i].state == 4 || peers[i].state == 6;
    return n;
}
static int finish_group(void);
static int begin_group(void);

/** @brief Clear keys through volatile stores. */
static void wipe(void *p, size_t n)
{
    volatile uint8_t *b = p;
    while (n--) *b++ = 0;
}
/** @brief Read a network-order halfword. */
static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
/** @brief Write a network-order halfword. */
static void put16(uint8_t *p, unsigned v) { p[0] = v >> 8; p[1] = v; }
/** @brief Compare authenticators without a data-dependent exit. */
static int equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    unsigned diff = 0;
    while (n--) diff |= *a++ ^ *b++;
    return diff == 0;
}
/** @brief Install a BIP-CMAC-128 key with an initial zero packet number. */
static int install_igtk(const uint8_t key[16], uint8_t index)
{
#if TIKU_ESP_SAE_ENABLE
    wifi_wpa_igtk_t k = {0};
    k.keyid[0] = index;
    memcpy(k.igtk, key, 16);
    int rc = esp_wifi_set_igtk_internal(WIFI_IF_AP, &k);
    wipe(&k, sizeof k);
    return rc;
#else
    (void)key; (void)index; return -1;
#endif
}
/** @brief Compute the negotiated EAPOL authenticator. */
static void key_mic(const ap_peer_t *p, const uint8_t *data, size_t len, uint8_t out[20])
{
#if TIKU_ESP_SAE_ENABLE
    if (ap.sae) { (void)espw_cmac(p->ptk, data, len, out); return; }
#endif
    tiku_kits_crypto_hmac_sha1(p->ptk, 16, data, len, out);
}
/** @brief Derive the CCMP pairwise key from the two MACs and nonces. */
static void derive(ap_peer_t *p)
{
    static const uint8_t label[] = "Pairwise key expansion";
    uint8_t data[76], digest[60];
    int lo = memcmp(ap.own, p->mac, 6) < 0;
    memcpy(data, lo ? ap.own : p->mac, 6);
    memcpy(data + 6, lo ? p->mac : ap.own, 6);
    lo = memcmp(p->anonce, p->snonce, 32) < 0;
    memcpy(data + 12, lo ? p->anonce : p->snonce, 32);
    memcpy(data + 44, lo ? p->snonce : p->anonce, 32);
#if TIKU_ESP_SAE_ENABLE
    if (ap.sae) { espw_ptk_sha256(p->pmk, data, p->ptk); return; }
#endif
    for (uint8_t i = 0; i < 3; i++) {
        tiku_kits_crypto_hmac_sha1_ctx_t h;
        tiku_kits_crypto_hmac_sha1_init(&h, p->pmk, 32);
        tiku_kits_crypto_hmac_sha1_update(&h, label, sizeof label);
        tiku_kits_crypto_hmac_sha1_update(&h, data, sizeof data);
        tiku_kits_crypto_hmac_sha1_update(&h, &i, 1);
        tiku_kits_crypto_hmac_sha1_final(&h, digest + 20 * i);
        wipe(&h, sizeof h);
    }
    memcpy(p->ptk, digest, 48);
    wipe(digest, sizeof digest);
}
/** @brief Send pairwise message 1/3 or group message 1 with retained exchange values. */
static int send_key(ap_peer_t *p)
{
    uint8_t frame[14 + 99 + 96] = {0}, plain[88] = {0}, digest[20];
    uint8_t *e = frame + 14, *k = e + 4;
    unsigned n = 99;
    memcpy(frame, p->mac, 6);
    memcpy(frame + 6, ap.own, 6);
    put16(frame + 12, 0x888e);
    e[0] = 2; e[1] = 3; k[0] = 2;
    put16(k + 1, (p->state == 1 ? 0x0088 : p->state == 4 ? 0x1380 : 0x13c8) |
                  (ap.sae ? 0 : 2));
    put16(k + 3, 16);
    k[9] = p->replay >> 24; k[10] = p->replay >> 16;
    k[11] = p->replay >> 8; k[12] = p->replay;
    memcpy(k + 13, p->anonce, 32);
    if (p->state == 2 || p->state == 4) {
        unsigned size = 0;
        if (p->state == 2) { memcpy(plain, ap.ie, ap.ie_len); size = ap.ie_len; }
        memcpy(plain + size, (uint8_t[]){221,22,0,15,172,1,0,0}, 8);
        plain[size + 6] = p->state == 4 ? 3 - ap.gtk_index : ap.gtk_index;
        memcpy(plain + size + 8, p->state == 4 ? ap.next_gtk : ap.gtk, 16);
        size += 24;
        if (ap.sae) {
            memcpy(plain + size, (uint8_t[]){221,28,0,15,172,9,0,0}, 8);
            plain[size + 6] = p->state == 4 ? 9 - ap.igtk_index : ap.igtk_index;
            memcpy(plain + size + 14, p->state == 4 ? ap.next_igtk : ap.igtk, 16);
            size += 30;
        }
        if (size % 8) { plain[size] = 221; size = (size + 7) & ~7u; }
        if (tiku_kits_crypto_aeskw_wrap(p->ptk + 16, 16, plain, size, k + 95)) {
            wipe(plain, sizeof plain); return -1;
        }
        n += size + 8;
        put16(k + 93, size + 8);
    }
    put16(e + 2, n - 4);
    if (p->state == 2 || p->state == 4) {
        key_mic(p, e, n, digest);
        memcpy(k + 77, digest, 16);
    }
    int r = esp_wifi_internal_tx(WIFI_IF_AP, frame, 14 + n);
    wipe(frame, sizeof frame); wipe(plain, sizeof plain); wipe(digest, sizeof digest);
    return r;
}
/** @brief Delete the departing station's keys and erase its handshake state. */
static bool remove_peer(uint8_t *mac)
{
    ap_peer_t *p = find_peer(mac);
    if (!p) return true;
    (void)esp_wifi_set_ap_key_internal(0, p->mac, 0, NULL, 0);
#if TIKU_ESP_SAE_ENABLE
    if (ap.sae) espw_sae_remove(true, p->mac);
#endif
    wipe(p, sizeof *p);
    return true;
}
/** @brief Drop one peer without changing another peer's keys. */
static void fail_peer(ap_peer_t *p)
{
    uint8_t mac[6]; memcpy(mac, p->mac, 6);
    (void)remove_peer(mac);
    (void)esp_wifi_ap_deauth_internal(mac, 15);
}
/** @brief Revoke peers when shared group keys cannot be installed. */
static void fail_group(void)
{
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++)
        if (peers[i].state) fail_peer(&peers[i]);
    wipe(ap.next_gtk, sizeof ap.next_gtk); wipe(ap.next_igtk, sizeof ap.next_igtk);
    ap.renewing = 0;
}
/** @brief Install group keys after each remaining member acknowledges them. */
static int finish_group(void)
{
    if (!ap.renewing) return 0;
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++)
        if (peers[i].state == 4) return 0;
    uint8_t index = 3 - ap.gtk_index;
    if ((ap.sae && install_igtk(ap.next_igtk, 9 - ap.igtk_index)) ||
        esp_wifi_set_ap_key_internal(3, broadcast, index, ap.next_gtk, 16)) {
        fail_group(); return -1;
    }
    ap.gtk_index = index; memcpy(ap.gtk, ap.next_gtk, 16);
    if (ap.sae) {
        ap.igtk_index = 9 - ap.igtk_index; memcpy(ap.igtk, ap.next_igtk, 16);
    }
    wipe(ap.next_gtk, sizeof ap.next_gtk); wipe(ap.next_igtk, sizeof ap.next_igtk);
    ap.renewing = 0;
    ap.deadline = tiku_clock_time() + ESPW_AP_REKEY_SECONDS * TIKU_CLOCK_SECOND;
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++) {
        ap_peer_t *p = &peers[i];
        if (p->state == 6) p->state = 3;
        if (p->state == 5) {
            p->state = 2; p->attempts = 1;
            p->deadline = tiku_clock_time() + TIKU_CLOCK_SECOND;
            (void)send_key(p);
        }
    }
    return 0;
}
/** @brief Send the same pending group keys through each member's pairwise channel. */
static int begin_group(void)
{
    if (ap.renewing) return 0;
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++)
        if (peers[i].state == 2) return 0;
    if (espw_osi_funcs._get_random(ap.next_gtk, 16) ||
        (ap.sae && espw_osi_funcs._get_random(ap.next_igtk, 16))) {
        fail_group(); return -1;
    }
    ap.renewing = 1;
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++) {
        ap_peer_t *p = &peers[i];
        if (p->state != 3) continue;
        if (p->replay == UINT32_MAX || espw_osi_funcs._get_random(p->anonce, 32)) {
            fail_peer(p); continue;
        }
        p->replay++; p->state = 4; p->attempts = 1;
        p->deadline = tiku_clock_time() + TIKU_CLOCK_SECOND;
        (void)send_key(p);
    }
    return finish_group();
}
/** @brief Initialize the AP's group key and beacon RSN element. */
static void *start(void)
{
    ap.enabled = 1;
    memset(peers, 0, sizeof peers);
    ap.renewing = ap.group_used = 0;
    ap.gtk_index = 1; ap.igtk_index = 4;
#if TIKU_ESP_SAE_ENABLE
    ap.sae = espw_sae_enabled(true);
    if (ap.sae) ap.secured = 1;
#endif
    if (esp_wifi_get_macaddr_internal(WIFI_IF_AP, ap.own)) goto fail;
    if (ap.secured) {
        if (espw_osi_funcs._get_random(ap.gtk, 16) ||
            esp_wifi_set_ap_key_internal(3, broadcast, 1, ap.gtk, 16)) goto fail;
        if (ap.sae && (espw_osi_funcs._get_random(ap.igtk, 16) ||
                       install_igtk(ap.igtk, 4))) goto fail;
        ap.ie_len = ap.sae ? sizeof sae_ie : sizeof ap_ie;
        memcpy(ap.ie, ap.sae ? sae_ie : ap_ie, ap.ie_len);
        if (esp_wifi_set_appie_internal(3, ap.ie, ap.ie_len, 0)) goto fail;
    }
    return &ap;
fail:
    ap.enabled = 0;
    return NULL;
}
/** @brief Stop admission and erase session keys when the AP stops. */
static bool stop(void *unused)
{
    (void)unused;
    (void)esp_wifi_unset_appie_internal(3);
    (void)esp_wifi_unset_appie_internal(2);
    wipe(&ap, sizeof ap);
    wipe(peers, sizeof peers);
    return true;
}
/** @brief Return the beacon's retained RSN bytes for a secured AP. */
static uint8_t *get_ie(size_t *n)
{
    *n = ap.secured ? ap.ie_len : 0;
    return ap.secured ? ap.ie : NULL;
}
/** @brief Validate one association and send its response before the key exchange. */
static bool join_peer(void *data)
{
    ap_join_t *j = data;
    if (!ap.enabled || !j || !j->sm || !j->mac || !j->pmf || !j->cipher ||
        (j->mac[0] & 1) || !memcmp(j->mac, (uint8_t[6]){0}, 6)) return false;
    ap_peer_t *p = find_peer(j->mac);
    if (p) return false;
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++)
        if (!peers[i].state) { p = &peers[i]; break; }
    if (!p) return false;
    wipe(p, sizeof *p);
    *j->sm = NULL;
    if (ap.secured) {
        if (ap.sae) {
#if TIKU_ESP_SAE_ENABLE
            uint8_t pmkid[16];
            if (!j->ie || (j->ie_len != 28 && j->ie_len != 44) ||
                j->ie[0] != 48 || j->ie[1] != j->ie_len - 2 ||
                memcmp(j->ie + 2, sae_ie + 2, 18) || !(j->ie[20] & 0x80) ||
                j->ie[23] || j->ie[22] != (j->ie_len == 44) ||
                memcmp(j->ie + j->ie_len - 4, sae_ie + 24, 4) ||
                !j->rsnxe || j->rsnxe_len != 3 || j->rsnxe[0] != 244 ||
                j->rsnxe[1] != 1 || !(j->rsnxe[2] & 0x20)) return false;
            if (espw_sae_get_key(true, j->mac, p->pmk, pmkid) ||
                (j->ie_len == 44 && !equal(j->ie + 24, pmkid, 16))) {
                /* Status 53 requires a cached-PMK client to authenticate again. */
                ap_assoc_t reject = {.ifx = WIFI_IF_AP, .subtype = j->subtype,
                                     .data_len = 2, .data = {53, 0}};
                memcpy(reject.da, j->mac, 6);
                (void)esp_wifi_send_mgmt_frm_internal(&reject);
                wipe(p->pmk, sizeof p->pmk);
                return false;
            }
#else
            return false;
#endif
        } else if (!j->ie || (j->ie_len != 22 && j->ie_len != 24 && j->ie_len != 28) ||
            j->ie[0] != 48 || j->ie[1] != j->ie_len - 2 ||
            memcmp(j->ie + 2, ap_ie + 2, 18) || (j->ie[20] & 0x40) ||
            (j->ie_len >= 24 && (j->ie[22] || j->ie[23])) ||
            (j->ie_len == 28 && memcmp(j->ie + 24, (uint8_t[]){0,15,172,6}, 4)))
            return false;
        memcpy(p->peer_ie, j->ie, j->ie_len);
        p->peer_ie_len = j->ie_len;
        if (ap.sae) { memcpy(p->peer_ie + j->ie_len, j->rsnxe, 3); p->peer_ie_len += 3; }
    }
    memcpy(p->mac, j->mac, 6);
    *j->pmf = ap.sae;
    /* The callback uses the WPA cipher bit index: CCMP is bit 3. */
    *j->cipher = ap.secured ? 3 : 0;
    ap_assoc_t reply = {.ifx = WIFI_IF_AP, .subtype = j->subtype, .data_len = 2};
    memcpy(reply.da, j->mac, 6);
    if (ap.sae && esp_wifi_set_appie_internal(2, ap.ie + 28, 3, 0)) return false;
    if (esp_wifi_send_mgmt_frm_internal(&reply)) return false;
    *j->sm = p;
    if (!ap.secured) { p->state = 3; return true; }
    if (!ap.sae) memcpy(p->pmk, ap.pmk, sizeof p->pmk);
    if (espw_osi_funcs._get_random(p->anonce, 32)) {
        p->state = 1; (void)remove_peer(p->mac); *j->sm = NULL; return false;
    }
#if TIKU_ESP_SAE_ENABLE
    if (ap.sae) espw_sae_bind(p->mac);
#endif
    p->replay++;
    p->state = 1;
    p->attempts = 1;
    p->deadline = tiku_clock_time() + TIKU_CLOCK_SECOND;
    (void)send_key(p);
    return true;
}
/** @brief Authenticate pairwise and group acknowledgements before installing a key once. */
static bool receive_key(void *ctx, void *sm, uint8_t *data, size_t len)
{
    ap_peer_t *p = NULL;
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++)
        if (sm == &peers[i]) { p = &peers[i]; break; }
    if (!p) return false;
    uint8_t f[99 + sizeof p->peer_ie], digest[20], mic[16];
    (void)ctx;
    if (!ap.enabled || !ap.secured ||
        (p->state != 1 && p->state != 2 && p->state != 4) || len < 99 || len > sizeof f ||
        !data || (data[0] != 1 && data[0] != 2) || data[1] != 3 ||
        be16(data + 2) != len - 4 || data[4] != 2) return false;
    const uint8_t *k = data + 4;
    unsigned info = be16(k + 1);
    uint8_t counter[8] = {0,0,0,0,p->replay >> 24,p->replay >> 16,p->replay >> 8,p->replay};
    if (!equal(counter, k + 5, 8) || be16(k + 93) != len - 99) return false;
    if (p->state == 1) {
        if (info != (ap.sae ? 0x0108u : 0x010au) || len != 99u + p->peer_ie_len ||
            memcmp(k + 95, p->peer_ie, p->peer_ie_len)) return false;
        memcpy(p->snonce, k + 13, 32);
        derive(p);
    } else if (info != ((p->state == 4 ? 0x0300u : 0x0308u) | (ap.sae ? 0u : 2u)) || len != 99) return false;
    memcpy(f, data, len); memcpy(mic, f + 81, 16); memset(f + 81, 0, 16);
    key_mic(p, f, len, digest);
    int ok = equal(mic, digest, 16);
    wipe(f, sizeof f); wipe(digest, sizeof digest);
    if (!ok) { if (p->state == 1) wipe(p->ptk, sizeof p->ptk); return false; }
    if (p->state == 1) {
        p->replay++; p->attempts = 1;
        p->deadline = tiku_clock_time() + TIKU_CLOCK_SECOND;
        if (ap.group_used || ap.renewing) {
            p->state = 5;
            return begin_group() == 0;
        }
        ap.group_used = 1;
        p->state = 2;
        (void)send_key(p);
        return true;
    }
    if (p->state == 4) {
        p->state = 6;
        return finish_group() == 0;
    }
    if (esp_wifi_set_ap_key_internal(3, p->mac, 0, p->ptk + 32, 16) ||
        !esp_wifi_wpa_ptk_init_done_internal(p->mac)) {
        fail_peer(p);
        return false;
    }
    /* Group exchanges retain KCK and KEK; the installed pairwise TK is erased. */
    wipe(p->ptk + 32, 16);
    p->state = 3;
    ap.deadline = tiku_clock_time() + ESPW_AP_REKEY_SECONDS * TIKU_CLOCK_SECOND;
    return true;
}
/** @brief This profile does not negotiate SPP A-MSDU protection. */
static void spp(void *sm, bool *cap, bool *req)
{
    (void)sm; *cap = false; *req = false;
}
/** @brief Renew authorized group keys and retry pending exchanges at one-second intervals. */
static int poll_task(void *unused)
{
    (void)unused;
#if TIKU_ESP_SAE_ENABLE
    espw_sae_poll();
#endif
    if (!ap.enabled) return 0;
    if (ap.secured && !ap.renewing && espw_ap_clients() &&
        !TIKU_CLOCK_LT(tiku_clock_time(), ap.deadline)) (void)begin_group();
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++) {
        ap_peer_t *p = &peers[i];
        if ((p->state == 1 || p->state == 2 || p->state == 4) &&
            !TIKU_CLOCK_LT(tiku_clock_time(), p->deadline)) {
            if (++p->attempts > 4) fail_peer(p);
            else {
                p->deadline = tiku_clock_time() + TIKU_CLOCK_SECOND;
                (void)send_key(p);
            }
        }
    }
    (void)finish_group();
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++)
        if (peers[i].state == 5) { (void)begin_group(); break; }
    return 0;
}
static volatile uint8_t poll_pending;
/** @brief Release the pending slot after the Wi-Fi task services AP deadlines. */
static int poll_run(void *unused)
{
    int rc = poll_task(unused);
    poll_pending = 0;
    return rc;
}
void espw_ap_poll(void)
{
    if (poll_pending) return;
    ap_ipc_t job = {.fn = poll_run};
    poll_pending = 1;
    if (esp_wifi_ipc_internal(&job, false)) poll_pending = 0;
}
void espw_ap_key(const uint8_t *pmk)
{
    wipe(&ap, sizeof ap);
    if (pmk) { memcpy(ap.pmk, pmk, 32); ap.secured = 1; }
}
void espw_ap_callbacks(wpa_funcs_t *cb)
{
    cb->wpa_ap_init = start;
    cb->wpa_ap_deinit = stop;
    cb->wpa_ap_join = join_peer;
    cb->wpa_ap_remove = remove_peer;
    cb->wpa_ap_get_wpa_ie = get_ie;
    cb->wpa_ap_rx_eapol = receive_key;
    cb->wpa_ap_get_peer_spp_msg = spp;
}
