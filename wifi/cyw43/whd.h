/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * whd.h - CYW43439 WHD protocol layer
 *
 * Sits on top of the gSPI transport (gspi.h) and implements:
 *
 *   - SDPCM framing: 12-byte header with length+inverse, sequence,
 *     channel/flags, header-offset, flow-control, credit byte. The
 *     chip ↔ host packet path is fully SDPCM-wrapped on F2.
 *
 *   - Per-channel dispatch: channel 0 = CONTROL (CDC IOCTL),
 *     channel 1 = EVENT (BDC + Broadcom OUI + EventMessage),
 *     channel 2 = DATA (BDC + Ethernet frame).
 *
 *   - CDC: 16-byte request/response header with command, length,
 *     flags (GET/SET, iface), id (echoed back), status. Used as the
 *     "tell chip to do X / read variable Y" RPC.
 *
 *   - SDPCM credit-based flow control: chip grants TX credits via
 *     the bus_data_credit field; host must not advance sdpcm_seq
 *     past sdpcm_seq_max.
 *
 * Phase 3 brings these mechanics up; phase 4 builds station-mode
 * join + scan IOCTLs on top. The runner loop (phase R.2) will own
 * the channel dispatch and async-event delivery.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_CYW43_WHD_H_
#define TIKU_DRV_WIFI_CYW43_WHD_H_

#include <stdint.h>
#include "kernel/drivers/tiku_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* WHD IOCTL — kind, command codes                                           */
/*---------------------------------------------------------------------------*/

/* CDC.flags low byte. */
#define WHD_IOCTL_KIND_GET 0U
#define WHD_IOCTL_KIND_SET 2U

/* WLC IOCTL command codes used in phase 3+. */
#define WHD_CMD_UP      2U     /* WLC_UP: bring radio up */
#define WHD_CMD_GET_VAR 262U
#define WHD_CMD_SET_VAR 263U

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bring up WHD on a chip that has already booted firmware.
 *
 * Runs phases 3.A through 4.A inline (matches the legacy probe
 * behaviour): F2 bus enable, SDPCM RX primer, GET("ver"), CLM
 * upload, radio defaults + MAC readback, event-channel reader,
 * single active scan.
 *
 * Pre-condition: cyw43_gspi_probe_chip_id() returned OK (firmware
 * is running on the chip, HT clock is up).
 *
 * Returns TIKU_DRV_OK on full success, else a TIKU_DRV_ERR_*
 * matching the failing sub-phase.
 */
int whd_init(void);

/**
 * @brief Issue one CDC IOCTL and wait for its matching response.
 *
 * @param kind_flags  WHD_IOCTL_KIND_GET / _SET
 * @param cmd_code    WLC command (WHD_CMD_GET_VAR / _SET_VAR / ...)
 * @param iface       0 for primary interface
 * @param tx_data     outbound payload (var name + value for SET,
 *                    var name + zero pad for GET); may be NULL
 * @param tx_len      bytes of tx_data; chip uses this as CDC.len
 * @param rx_data     buffer for response payload; may be NULL
 * @param rx_size     capacity of rx_data
 * @param rx_len_out  receives the chip-reported response length;
 *                    may be NULL
 *
 * Returns TIKU_DRV_OK on chip status=0, else an error code.
 */
int whd_ioctl(uint16_t kind_flags, uint32_t cmd_code, uint16_t iface,
              const uint8_t *tx_data, uint32_t tx_len,
              uint8_t *rx_data, uint32_t rx_size,
              uint32_t *rx_len_out);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_DRV_WIFI_CYW43_WHD_H_ */
