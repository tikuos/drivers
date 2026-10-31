/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp_sae.h - Native Wi-Fi SAE callbacks and password-element ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ESP_SAE_H_
#define ESP_SAE_H_
#include "esp_abi.h"
#include "sae/esp_sae_engine.h"
/** @brief Replace the owned password element while the radio is stopped or disconnected. */
void espw_sae_configure(void *pt, bool access_point);
/** @brief Erase the active exchange, retaining the configured password element. */
void espw_sae_leave(void);
/** @brief Release configuration and exchange allocations before radio heap teardown. */
void espw_sae_stop(void);
/** @brief Whether SAE is configured for the specified interface. */
bool espw_sae_enabled(bool access_point);
/** @brief Retrieve authenticated key material for the matching peer. */
int espw_sae_peer_key(const uint8_t peer[6], uint8_t pmk[32], uint8_t pmkid[16]);
/** @brief Bind authenticated AP keys to an admitted association. */
void espw_sae_bind(const uint8_t peer[6]);
/** @brief Remove one peer or, with NULL, every exchange on the selected interface. */
void espw_sae_remove(bool access_point, const uint8_t *peer);
/** @brief Retrieve authenticated key material on the selected interface. */
int espw_sae_get_key(bool access_point, const uint8_t peer[6],
                     uint8_t pmk[32], uint8_t pmkid[16]);
/** @brief Expire incomplete AP authentication on the Wi-Fi task. */
void espw_sae_poll(void);
/** @brief Register station and AP authentication callbacks. */
void espw_sae_callbacks(wpa_funcs_t *cb);
#endif
