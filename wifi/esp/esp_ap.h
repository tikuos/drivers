/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * esp_ap.h - C6 access-point authenticator bindings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP_AP_H_
#define TIKU_ESP_AP_H_
#include "esp_abi.h"
#define ESPW_AP_MAX_CLIENTS 4u
/** @brief Count authorized clients, including those renewing their group keys. */
uint8_t espw_ap_clients(void);
/** @brief Install open/WPA2/SAE authenticator callbacks into the radio's table. */
void espw_ap_callbacks(wpa_funcs_t *cb);
/** @brief Set the WPA2 PMK before AP start; configured SAE supplies peer PMKs. */
void espw_ap_key(const uint8_t *pmk);
/** @brief Queue retries and group renewal on the Wi-Fi task without waiting. */
void espw_ap_poll(void);
#endif
