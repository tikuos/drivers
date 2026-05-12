/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * whd.h - CYW43439 WHD protocol layer + runner process
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
 *     flags (GET/SET, iface), id (echoed back), status.
 *
 *   - SDPCM credit-based flow control via the bus_data_credit field.
 *
 * After R.2 the protocol layer is owned by a TikuOS process
 * (`cyw43_runner`). The driver init kicks the process; it runs the
 * bring-up sequence synchronously on its first wake-up and then
 * sits in an event loop, dispatching scan/join/status requests from
 * elsewhere in the system.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_CYW43_WHD_H_
#define TIKU_DRV_WIFI_CYW43_WHD_H_

#include <stdint.h>
#include "kernel/drivers/tiku_drv.h"
#include "kernel/process/tiku_process.h"

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
/* Runner-process events                                                     */
/*---------------------------------------------------------------------------*/

/* Events posted to the cyw43_runner process. Values share the user-
 * event namespace starting at TIKU_EVENT_USER (0x10). */
#define CYW43_WIFI_EVT_SCAN_START  (TIKU_EVENT_USER + 0U)

/*---------------------------------------------------------------------------*/
/* Public status snapshot                                                    */
/*---------------------------------------------------------------------------*/

typedef struct {
    uint8_t  up;              /* 1 after whd bring-up completes */
    uint8_t  scan_in_progress;
    uint16_t scan_aps_found;  /* count from last completed scan */
    uint8_t  mac[6];          /* chip's OTP-burned MAC (valid once up) */
} cyw43_wifi_status_t;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Start the WHD runner process.
 *
 * Pre-condition: cyw43_gspi_probe_chip_id() returned OK (firmware
 * is running on the chip, HT clock is up).
 *
 * The runner runs phases 3.A through 3.E synchronously on its
 * first dispatch, then enters an event loop. After bring-up
 * completes a SCAN_START event is self-posted so the boot-time
 * sanity scan still happens.
 *
 * Returns TIKU_DRV_OK if the process was started, else an error.
 */
int whd_runner_init(void);

/**
 * @brief Request a scan. Non-blocking — posts an event to the
 *        runner which performs the scan synchronously on its
 *        next dispatch.
 *
 * Returns TIKU_DRV_OK on enqueue, TIKU_DRV_ERR_TIMEOUT if the
 * runner's event queue is full.
 */
int cyw43_wifi_scan_start(void);

/**
 * @brief Snapshot the WHD layer's state. Synchronous read.
 */
int cyw43_wifi_status(cyw43_wifi_status_t *out);

/**
 * @brief Issue one CDC IOCTL and wait for its matching response.
 *
 * Exposed for callers outside the runner (e.g. future tooling /
 * tests). Note that this is blocking; calling it from inside
 * another process body will stall that process for the IOCTL
 * round-trip. Phase R.3 will swap the busy-waits for timer yields.
 */
int whd_ioctl(uint16_t kind_flags, uint32_t cmd_code, uint16_t iface,
              const uint8_t *tx_data, uint32_t tx_len,
              uint8_t *rx_data, uint32_t rx_size,
              uint32_t *rx_len_out);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_DRV_WIFI_CYW43_WHD_H_ */
