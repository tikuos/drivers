/*
 * Tiku Drivers - ESP32-C61 BLE over Espressif's LE controller library
 *
 * The controller runs from the XIP window beside the boot image and carries
 * tikuOS's own host stack (tikukits/net/bluetooth) over its in-memory HCI;
 * see drivers/wifi/esp/README.md for fetching the library.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_BLE_ESP_H_
#define TIKU_DRV_BLE_ESP_H_

#include <stdint.h>

#include "kernel/drivers/tiku_drv.h"

/** @brief The driver's descriptor, listed in drivers/tiku_drv_table.c. */
extern const tiku_drv_t tiku_drv_ble_esp;

/**
 * @brief The controller on (the host stack brought up over it) or off
 *        (every byte back).  Kernel thread only.
 *        @return TIKU_DRV_OK, or a TIKU_DRV_ERR_* (said why)
 */
int tiku_drv_ble_esp_power(uint8_t on);

/** @brief What `bt status` shows of the controller. */
typedef struct {
    uint8_t     up;
    const char *version;        /* the library's build id */
    uint32_t    heap_used;
    uint32_t    heap_size;
    uint32_t    heap_peak;
    uint32_t    rx_dropped;     /* HCI packets the host had no room for */
    uint32_t    irqs;           /* the radios' interrupts since boot */
} tiku_drv_ble_esp_status_t;

/** @brief Fill @p out. */
void tiku_drv_ble_esp_status(tiku_drv_ble_esp_status_t *out);

#endif /* TIKU_DRV_BLE_ESP_H_ */
