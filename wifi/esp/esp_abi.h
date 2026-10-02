/*
 * Tiku Drivers - ESP32-C61 radio libraries, the ABI this driver uses
 *
 * Hand-written from ESP-IDF 4d59230 (OS adapter version 9), the tree whose
 * library commits fetch.sh pins: only what the driver calls or defines.
 * Each structure added later carries size and offset asserts checked
 * against IDF's own headers, read as reference and never included.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_ESP_ABI_H_
#define TIKU_DRV_WIFI_ESP_ABI_H_

#include <stdint.h>

/** @brief libphy: the PHY's version, formatted into its own buffer. */
const char *get_phy_version_str(void);

/** @brief Each library's git revision, set at its build. */
extern const char *libnet80211_reversion_git;
extern const char *libpp_reversion_git;
extern const char *libcore_reversion_git;

/** @brief libnet80211: start the stack from a wifi_init_config_t. */
int esp_wifi_init_internal(const void *config);

#endif /* TIKU_DRV_WIFI_ESP_ABI_H_ */
