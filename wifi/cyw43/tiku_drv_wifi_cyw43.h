/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_wifi_cyw43.h - CYW43439 WiFi driver public interface
 *
 * Clean-room driver for the Infineon CYW43439 (Pi Pico 2 W radio).
 * Currently phase 0: registry shell only. Phase-by-phase plan
 * tracked in README.md alongside this header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_CYW43_H_
#define TIKU_DRV_WIFI_CYW43_H_

#include "kernel/drivers/tiku_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Descriptor symbol — referenced by an extern in
 * tikudrivers/tiku_drv_table.c. The kernel finds the driver
 * through this descriptor; nothing else in this header is
 * load-bearing for v0.
 */
extern const tiku_drv_t tiku_drv_wifi_cyw43;

/*
 * Public API additions (scan/join/send/status calls) will land
 * here as phases 3..5 fill in the WHD protocol. Keep this header
 * deliberately small for now — application code that wants WiFi
 * state should go through the `/dev/wifi/wifi0/...` VFS nodes
 * once those arrive in phase 6, not through direct calls.
 */

#ifdef __cplusplus
}
#endif

#endif /* TIKU_DRV_WIFI_CYW43_H_ */
