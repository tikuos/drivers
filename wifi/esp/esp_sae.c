/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp_sae.c - Independent station and AP SAE authentication callbacks.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_sae.h"
#include "esp_ap.h"
#include "esp_port.h"
#include "esp_heap.h"
#include <string.h>
static void *password_element[2];
typedef struct {
    struct espw_sae_session *session;
    uint8_t mac[6];
    bool bound;
    tiku_clock_time_t deadline;
} sae_peer_t;
static sae_peer_t station, clients[ESPW_AP_MAX_CLIENTS];
extern bool esp_wifi_ap_notify_node_sae_auth_done(uint8_t *mac);
extern int esp_wifi_send_mgmt_frm_internal(const void *request);

/** @brief Locate an allocated exchange in the selected interface. */
static sae_peer_t *lookup(bool ap, const uint8_t *mac)
{
    sae_peer_t *p = ap ? clients : &station;
    for (unsigned i = 0; i < (ap ? ESPW_AP_MAX_CLIENTS : 1); i++)
        if (p[i].session && !memcmp(p[i].mac, mac, 6)) return &p[i];
    return NULL;
}
/** @brief Release one exchange and its identity. */
static void clear(sae_peer_t *p)
{
    espw_sae_session_free(p->session);
    memset(p, 0, sizeof *p);
}
int espw_sae_random(uint8_t *p, size_t n) { return espw_osi_funcs._get_random(p, n); }
void espw_sae_remove(bool ap, const uint8_t *mac)
{
    sae_peer_t *p = ap ? clients : &station;
    for (unsigned i = 0; i < (ap ? ESPW_AP_MAX_CLIENTS : 1); i++)
        if (!mac || !memcmp(p[i].mac, mac, 6)) clear(&p[i]);
}
void espw_sae_leave(void) { espw_sae_remove(false, NULL); }
void espw_sae_stop(void)
{
    for (unsigned i = 0; i < 2; i++) {
        espw_sae_remove(i != 0, NULL);
        espw_sae_pt_free(password_element[i]); password_element[i] = NULL;
    }
}
void espw_sae_configure(void *pt, bool ap)
{
    espw_sae_remove(ap, NULL);
    espw_sae_pt_free(password_element[ap]); password_element[ap] = pt;
}
bool espw_sae_enabled(bool ap) { return password_element[ap] != NULL; }
int espw_sae_get_key(bool ap, const uint8_t peer[6], uint8_t pmk[32], uint8_t pmkid[16])
{
    sae_peer_t *p = peer ? lookup(ap, peer) : NULL;
    return p ? espw_sae_key(p->session, pmk, pmkid) : -1;
}
int espw_sae_peer_key(const uint8_t peer[6], uint8_t pmk[32], uint8_t pmkid[16])
{
    return espw_sae_get_key(false, peer, pmk, pmkid);
}
void espw_sae_bind(const uint8_t peer[6])
{
    sae_peer_t *p = lookup(true, peer);
    if (p) p->bound = true;
}
void espw_sae_poll(void)
{
    for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++)
        if (clients[i].session && !clients[i].bound &&
            !TIKU_CLOCK_LT(tiku_clock_time(), clients[i].deadline)) clear(&clients[i]);
}
/** @brief Start an exchange using the selected interface's password element. */
static int begin(sae_peer_t *p, bool ap, const uint8_t *peer)
{
    uint8_t own[6];
    if (!peer || !password_element[ap] ||
        esp_wifi_get_macaddr_internal(ap ? WIFI_IF_AP : WIFI_IF_STA, own)) return -1;
    p->session = espw_sae_session_new(password_element[ap], own, peer);
    if (!p->session) return -1;
    memcpy(p->mac, peer, 6);
    p->deadline = tiku_clock_time() + 5 * TIKU_CLOCK_SECOND;
    return 0;
}
/** @brief Return a retained station authentication message for vendor retries. */
static uint8_t *sta_build(uint8_t *bssid, uint32_t type, size_t *len)
{
    if (!len) return NULL;
    *len = 0;
    if (!espw_sae_enabled(false) || !bssid || (type != 1 && type != 2)) return NULL;
    if (station.session && memcmp(station.mac, bssid, 6)) clear(&station);
    if (!station.session && (type != 1 || begin(&station, false, bssid))) return NULL;
    return (uint8_t *)espw_sae_message(station.session, type, len);
}
/** @brief Require H2E commits and authenticated confirms. */
static int sta_parse(uint8_t *buf, size_t len, uint32_t type, uint16_t status)
{
    if (!espw_sae_enabled(false) || !station.session) return -1;
    if (type == 1 && status == 76) return espw_sae_token(station.session, buf, len);
    if (type == 1 && status == 126) return espw_sae_commit(station.session, buf, len);
    if (type == 2 && status == 0) return espw_sae_confirm(station.session, buf, len);
    return -1;
}
/** @brief Send an AP authentication body through the native management-frame API. */
static int ap_reply(sae_peer_t *peer, unsigned type, unsigned status)
{
    size_t n;
    const uint8_t *p = espw_sae_message(peer->session, type, &n);
    if (!p || n > 360) return -1;
    struct request {
        wifi_interface_t ifx;
        uint8_t subtype;
        uint32_t len;
        uint8_t da[6], data[];
    };
    struct request *r = espw_calloc(1, sizeof *r + 6 + n);
    if (!r) return -1;
    r->ifx = WIFI_IF_AP; r->subtype = 0xb0; r->len = 6 + n;
    memcpy(r->da, peer->mac, 6);
    r->data[0] = 3; r->data[2] = type; r->data[4] = status;
    memcpy(r->data + 6, p, n);
    int rc = esp_wifi_send_mgmt_frm_internal(r);
    espw_free(r);
    return rc;
}
/** @brief Authenticate AP peers without replacing another peer's exchange. */
static int ap_auth(uint8_t *buf, size_t len, uint32_t type, uint16_t status, uint8_t *peer)
{
    if (!espw_sae_enabled(true) || !peer || !buf || (peer[0] & 1) ||
        !memcmp(peer, (uint8_t[6]){0}, 6)) return -1;
    sae_peer_t *p = lookup(true, peer);
    if (p && p->bound) return -1;
    if (type == 1 && status == 126) {
        if (!p) {
            espw_sae_poll();
            for (unsigned i = 0; i < ESPW_AP_MAX_CLIENTS; i++)
                if (!clients[i].session) { p = &clients[i]; break; }
            if (!p || begin(p, true, peer)) return -1;
        }
        if (espw_sae_commit(p->session, buf, len)) { clear(p); return -1; }
        return ap_reply(p, 1, 126);
    }
    if (type == 2 && status == 0 && p && !espw_sae_confirm(p->session, buf, len)) {
        if (ap_reply(p, 2, 0) || !esp_wifi_ap_notify_node_sae_auth_done(peer)) return -1;
        return 0;
    }
    return -1;
}
void espw_sae_callbacks(wpa_funcs_t *cb)
{
    cb->wpa3_build_sae_msg = sta_build;
    cb->wpa3_parse_sae_msg = sta_parse;
    cb->wpa3_hostap_handle_auth = ap_auth;
}
