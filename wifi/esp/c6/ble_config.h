/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * ble_config.h - C6 Bluetooth diagnostics through the remote console.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_BLE_CONFIG_H
#define TIKU_C6_BLE_CONFIG_H
#ifndef __ASSEMBLER__
int espw_c6_log(const char *format, ...);
#define TIKU_BT_PRINTF(...) espw_c6_log("[bt] " __VA_ARGS__)
#endif
#endif
