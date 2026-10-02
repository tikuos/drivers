/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * bt_transport.h — CYW43439 Bluetooth transport (BTSDIO) driver
 *
 * Implements the @ref tiku_bt_transport_t vtable from
 * interfaces/bluetooth/tiku_bt_transport.h on top of the CYW43439's
 * BTSDIO ring buffers in WLAN RAM. Only the BTFW upload + ring
 * buffer handshake + per-packet encode/decode lives here; the BLE
 * protocol stack on top (HCI / L2CAP / ATT / GATT / GAP / SMP) is
 * driver-independent and lives in tikukits/net/bluetooth/.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CYW43_BT_TRANSPORT_H_
#define TIKU_CYW43_BT_TRANSPORT_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-shot bring-up of the CYW43439 BT subsystem
 *
 * Sequence:
 *   1. BT2WLAN_PWRUP                   (chip-side power on)
 *   2. BTFW upload (Intel-HEX records) (drivers/wifi/cyw43/firmware.S)
 *   3. Wait for BT_CTRL.FW_RDY         (~300 ms budget)
 *   4. Read WLAN_RAM_BASE, zero rings
 *   5. Wait for BT_CTRL.BT_AWAKE
 *   6. HOST_CTRL.SW_RDY + DATA_VALID toggle
 *   7. Register tiku_bt_transport_t vtable
 *   8. Hand off to tiku_bt_init() for HCI Reset / identity queries /
 *      demo service registration
 *
 * Called from drivers/wifi/cyw43/whd.c::cyw43_runner after WHD
 * (WiFi side) bring-up succeeds.
 *
 * @return TIKU_DRV_OK on full success; otherwise the failing-step rc.
 */
int cyw43_bt_init(void);

/**
 * @brief The BTFW version string from the firmware blob's header (the
 *        transport's version hook).
 */
const char *cyw43_bt_fw_version(void);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_CYW43_BT_TRANSPORT_H_ */
