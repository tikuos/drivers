/*
 * Tiku Drivers
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * wifi_s31_profile.h - Encrypted native station or AP profile ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WIFI_S31_PROFILE_H_
#define TIKU_WIFI_S31_PROFILE_H_
#include <stdint.h>
/** @brief Cache admitted credentials in RAM; strings must satisfy the wireless API bounds. */
void espw_s31_profile_remember(const char *ssid, const char *psk, unsigned auth,
                               unsigned ap, uint8_t channel);
/** @brief Erase saved credentials and their RAM copy; return a driver result. */
int espw_s31_profile_forget(void);
/** @brief Apply profile-owned power changes in kernel foreground context. */
int espw_s31_profile_power(uint8_t on);
/** @brief Whether a profile operation owns credentials and radio state. */
int espw_s31_profile_busy(void);
#endif
