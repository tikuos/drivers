/*
 * Tiku Drivers - ESP32-C61 Wi-Fi over Espressif's radio libraries
 *
 * The libraries run from the XIP window beside the boot image; see
 * drivers/wifi/esp/README.md for fetching them and the milestones.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_ESP_H_
#define TIKU_DRV_WIFI_ESP_H_

#include "kernel/drivers/tiku_drv.h"

/** @brief The driver's descriptor, listed in drivers/tiku_drv_table.c. */
extern const tiku_drv_t tiku_drv_wifi_esp;

#endif /* TIKU_DRV_WIFI_ESP_H_ */
