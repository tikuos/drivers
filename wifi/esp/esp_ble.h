/*
 * Tiku Drivers - ESP32-C61 BLE, what its pieces share
 *
 * esp_npl.c is the OS the controller sees, esp_mempool.c the memory pools it
 * imports, esp_ble.c its life cycle and its HCI.  Internal: nothing outside
 * drivers/wifi/esp includes this.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_ESP_BLE_H_
#define TIKU_DRV_WIFI_ESP_BLE_H_

#include <stdint.h>

#include "esp_ble_abi.h"

/** @brief The NPL table registered with the controller. */
extern espb_npl_funcs_t espb_npl_funcs;

/** @brief Make the NPL object pools, as many of each as @p n says the
 *         controller will make.  @return 0, or -1 without memory */
int espb_npl_init(const espb_npl_counts_t *n);

/** @brief Give the pools back; a callout still armed is disarmed first. */
void espb_npl_deinit(void);

/** @brief The controller's task is about to block, its work done: the host
 *         may go on (called by the NPL before every blocking wait). */
void espb_controller_idle(void);

/** @brief Each kind's objects (event, queue, callout, semaphore, mutex):
 *         in use now, the most at once, and how many were made. */
void espb_npl_stats(uint16_t live[5], uint16_t peak[5], uint16_t made[5]);

#endif /* TIKU_DRV_WIFI_ESP_BLE_H_ */
