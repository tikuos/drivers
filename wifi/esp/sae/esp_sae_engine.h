/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp_sae_engine.h - Bounded group-19 SAE hash-to-element exchanges.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ESP_SAE_ENGINE_H_
#define ESP_SAE_ENGINE_H_
#include <stddef.h>
#include <stdint.h>
struct espw_sae_session;
/** @brief Derive an H2E password element; caller frees it after its sessions end. */
void *espw_sae_pt_new(const uint8_t *ssid, size_t slen, const uint8_t *password, size_t plen);
/** @brief Erase and release a password element; NULL is accepted. */
void espw_sae_pt_free(void *pt);
/** @brief Create a randomized exchange for two distinct unicast MACs. */
struct espw_sae_session *espw_sae_session_new(void *pt, const uint8_t own[6], const uint8_t peer[6]);
/** @brief Erase and free an exchange; NULL is accepted. */
void espw_sae_session_free(struct espw_sae_session *s);
/** @brief Get retained commit (1) or confirm (2) bytes, or NULL for invalid state. */
const uint8_t *espw_sae_message(struct espw_sae_session *s, unsigned type, size_t *len);
/** @brief Process an H2E commit; return zero, or -1 for an invalid exchange. */
int espw_sae_commit(struct espw_sae_session *s, const uint8_t *p, size_t len);
/** @brief Authenticate a confirm; matching retransmissions return zero without key changes. */
int espw_sae_confirm(struct espw_sae_session *s, const uint8_t *p, size_t len);
/** @brief Add a group-19 H2E anti-clogging token to an unprocessed commit. */
int espw_sae_token(struct espw_sae_session *s, const uint8_t *p, size_t len);
/** @brief Copy confirmed PMK/PMKID; return -1 until the peer's confirm is authenticated. */
int espw_sae_key(struct espw_sae_session *s, uint8_t pmk[32], uint8_t pmkid[16]);
/** @brief Read radio entropy for scalar generation and multiplication blinding. */
int espw_sae_random(uint8_t *p, size_t n);
#endif
