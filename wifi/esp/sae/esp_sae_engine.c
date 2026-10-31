/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp_sae_engine.c - Group-19 H2E commit, confirm and retransmission state.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_sae_engine.h"
#include "common.h"
#include "common/defs.h"
#include "common/sae.h"
#include "utils/wpabuf.h"
#include "crypto/sha256.h"
#include "crypto/crypto.h"

struct espw_sae_session {
    struct sae_data sae;
    struct wpabuf *commit, *confirm;
    uint8_t peer_commit_hash[32], peer_confirm[34];
};
static int groups[] = {19, 0};

void *espw_sae_pt_new(const uint8_t *ssid, size_t slen, const uint8_t *password, size_t plen)
{
    if (!ssid || !password || !slen || slen > 32 || !plen || plen > 63) return NULL;
    return sae_derive_pt(groups, ssid, slen, password, plen, NULL, 0);
}
void espw_sae_pt_free(void *pt) { sae_deinit_pt(pt); }

void espw_sae_session_free(struct espw_sae_session *s)
{
    if (!s) return;
    wpabuf_clear_free(s->commit);
    wpabuf_clear_free(s->confirm);
    sae_clear_data(&s->sae);
    bin_clear_free(s, sizeof *s);
}
struct espw_sae_session *espw_sae_session_new(void *pt, const uint8_t own[6], const uint8_t peer[6])
{
    if (!pt || !own || !peer || (own[0] & 1) || (peer[0] & 1) || !memcmp(own, peer, 6))
        return NULL;
    struct espw_sae_session *s = os_zalloc(sizeof *s);
    if (!s) return NULL;
    if (sae_set_group(&s->sae, 19)) { espw_sae_session_free(s); return NULL; }
    s->sae.akmp = WPA_KEY_MGMT_SAE;
    s->sae.no_pw_id = 1;
    s->commit = wpabuf_alloc(360);
    s->confirm = wpabuf_alloc(34);
    if (!s->commit || !s->confirm ||
        sae_prepare_commit_pt(&s->sae, pt, own, peer, NULL, NULL) ||
        sae_write_commit(&s->sae, s->commit, NULL, NULL, 0)) {
        espw_sae_session_free(s); return NULL;
    }
    s->sae.state = SAE_COMMITTED;
    return s;
}
const uint8_t *espw_sae_message(struct espw_sae_session *s, unsigned type, size_t *len)
{
    if (!len) return NULL;
    *len = 0;
    if (!s || (type != 1 && type != 2)) return NULL;
    struct wpabuf *b = type == 1 ? s->commit : s->confirm;
    if (!b || !wpabuf_len(b)) return NULL;
    *len = wpabuf_len(b);
    return wpabuf_head(b);
}
int espw_sae_commit(struct espw_sae_session *s, const uint8_t *p, size_t len)
{
    uint8_t hash[32];
    if (!s || !p || len < 98 || len > 360) return -1;
    if (sha256_vector(1, &p, &len, hash)) return -1;
    if (s->sae.state != SAE_COMMITTED)
        return os_memcmp_const(hash, s->peer_commit_hash, sizeof hash) ? -1 : 0;
    if (sae_parse_commit(&s->sae, p, len, NULL, NULL, groups, 1, NULL)) return -1;
    const struct wpabuf *r = s->sae.tmp->peer_rejected_groups;
    if (r) {
        const uint8_t *q = wpabuf_head(r);
        if (wpabuf_len(r) % 2) return -1;
        for (size_t i = 0; i < wpabuf_len(r); i += 2)
            if (WPA_GET_LE16(q + i) == 19) return -1;
    }
    if (sae_process_commit(&s->sae) || sae_write_confirm(&s->sae, s->confirm)) return -1;
    memcpy(s->peer_commit_hash, hash, sizeof hash);
    s->sae.state = SAE_CONFIRMED;
    return 0;
}
int espw_sae_confirm(struct espw_sae_session *s, const uint8_t *p, size_t len)
{
    if (!s || !p || len != 34) return -1;
    if (s->sae.state == SAE_ACCEPTED)
        return os_memcmp_const(p, s->peer_confirm, len) ? -1 : 0;
    if (s->sae.state != SAE_CONFIRMED || !WPA_GET_LE16(p) ||
        sae_check_confirm(&s->sae, p, len, NULL)) return -1;
    memcpy(s->peer_confirm, p, len);
    s->sae.state = SAE_ACCEPTED;
    sae_clear_temp_data(&s->sae);
    return 0;
}
int espw_sae_token(struct espw_sae_session *s, const uint8_t *p, size_t len)
{
    if (!s || !p || s->sae.state != SAE_COMMITTED || len < 6 || len > 259 ||
        WPA_GET_LE16(p) != 19 || p[2] != 255 || p[3] != len - 4 || p[4] != 93)
        return -1;
    struct wpabuf *token = wpabuf_alloc_copy(p + 5, len - 5);
    if (!token) return -1;
    s->commit->used = 0;
    int r = sae_write_commit(&s->sae, s->commit, token, NULL, 0);
    wpabuf_free(token);
    return r;
}
int espw_sae_key(struct espw_sae_session *s, uint8_t pmk[32], uint8_t pmkid[16])
{
    if (!s || !pmk || !pmkid || s->sae.state != SAE_ACCEPTED || s->sae.pmk_len != 32)
        return -1;
    memcpy(pmk, s->sae.pmk, 32);
    memcpy(pmkid, s->sae.pmkid, 16);
    return 0;
}
