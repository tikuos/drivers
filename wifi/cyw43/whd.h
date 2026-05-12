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
#include "interfaces/wireless/tiku_wireless.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* WHD IOCTL — kind, command codes                                           */
/*---------------------------------------------------------------------------*/

/* CDC.flags low byte. */
#define WHD_IOCTL_KIND_GET 0U
#define WHD_IOCTL_KIND_SET 2U

/* WLC IOCTL command codes used in phase 3+. The CYW43 firmware
 * exposes some join knobs as raw WLC commands (not iovars), so
 * SetInfra/SetAuth/SetWsec/SetWpaAuth go via WHD_CMD_* directly,
 * NOT via WHD_CMD_SET_VAR. Mixing the two paths gets WL_BADARG. */
#define WHD_CMD_UP            2U    /* WLC_UP: bring radio up */
#define WHD_CMD_SET_INFRA    20U    /* WLC_SET_INFRA: STA(1) / AP(0) */
#define WHD_CMD_SET_AUTH     22U    /* WLC_SET_AUTH: AUTH_OPEN(0)/SAE(3) */
#define WHD_CMD_SET_SSID     26U    /* WLC_SET_SSID — triggers join */
#define WHD_CMD_DISASSOC     52U    /* WLC_DISASSOC */
#define WHD_CMD_SET_WSEC    134U    /* WLC_SET_WSEC: AES(4) / TKIP(2) bitmask */
#define WHD_CMD_SET_WPA_AUTH 165U   /* WLC_SET_WPA_AUTH: WPA2-PSK=0x80 */
#define WHD_CMD_GET_VAR     262U
#define WHD_CMD_SET_VAR     263U
#define WHD_CMD_SET_WSEC_PMK 268U   /* WLC_SET_WSEC_PMK — passphrase */

/*---------------------------------------------------------------------------*/
/* Runner-process events                                                     */
/*---------------------------------------------------------------------------*/

/* Event IDs and result type live in interfaces/wireless/tiku_wireless.h.
 * The old CYW43_WIFI_EVT_* / cyw43_ap_t / cyw43_wifi_status_t names are
 * kept as aliases below so existing callers don't need a flag-day rename. */
#define CYW43_WIFI_EVT_SCAN_START     TIKU_WIRELESS_EVT_SCAN_START
#define CYW43_WIFI_EVT_SCAN_COMPLETE  TIKU_WIRELESS_EVT_SCAN_COMPLETE
#define CYW43_WIFI_EVT_AP_FOUND       TIKU_WIRELESS_EVT_AP_FOUND
#define CYW43_WIFI_EVT_LINK_UP        TIKU_WIRELESS_EVT_LINK_UP
#define CYW43_WIFI_EVT_LINK_DOWN      TIKU_WIRELESS_EVT_LINK_DOWN

/*---------------------------------------------------------------------------*/
/* Public status snapshot                                                    */
/*---------------------------------------------------------------------------*/

/* Driver-side aliases for the kernel-interface types/limits. */
#define CYW43_MAX_SCAN_RESULTS TIKU_WIRELESS_MAX_SCAN_RESULTS
typedef tiku_wireless_ap_t      cyw43_ap_t;
typedef tiku_wireless_status_t  cyw43_wifi_status_t;

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

/*
 * Driver-side aliases. The kernel-level API in
 * interfaces/wireless/tiku_wireless.h is the source of truth; these
 * wrappers exist so historic callers (the shell command, etc.) keep
 * compiling. New code should call tiku_wireless_* directly.
 */
static inline int cyw43_wifi_scan_start(void)
{ return tiku_wireless_scan_start(); }

static inline int cyw43_wifi_status(cyw43_wifi_status_t *out)
{ return tiku_wireless_status(out); }

static inline uint8_t
cyw43_wifi_scan_results(cyw43_ap_t *out, uint8_t max)
{ return tiku_wireless_scan_results(out, max); }

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

/*---------------------------------------------------------------------------*/
/* Data-path API (phase 4.C)                                                 */
/*---------------------------------------------------------------------------*/

/** Maximum Ethernet payload we accept from / deliver to the chip. */
#define WHD_DATA_MAX_FRAME 1514U

/**
 * @brief RX callback type. Invoked synchronously from the runner
 *        process context when a channel-2 (DATA) SDPCM frame arrives.
 *
 *        The pointer @p frame is valid only for the duration of the
 *        call — the callback MUST copy bytes it needs to retain. The
 *        runner's RX buffer is overwritten on the next poll.
 *
 *        @p ctx is the cookie passed at registration time.
 */
typedef void (*whd_rx_eth_cb_t)(const uint8_t *frame, uint16_t len,
                                void *ctx);

/**
 * @brief Register a single RX callback for Ethernet frames coming off
 *        the chip.  Replaces any previously installed callback.
 *        Pass cb=NULL to unregister.
 *
 *        Lives below the kernel surface — the link-layer adapter in
 *        tikukits/net/wifi/ is the intended consumer.
 */
int whd_register_rx_callback(whd_rx_eth_cb_t cb, void *ctx);

/**
 * @brief Transmit one Ethernet frame.  Synchronous, completes when
 *        the gSPI transfer to the chip finishes.  Returns:
 *          TIKU_DRV_OK         on success
 *          TIKU_DRV_ERR_INVALID  bad args / frame too big
 *          TIKU_DRV_ERR_TIMEOUT  out of SDPCM credit (chip ahead) —
 *                                caller may retry after RX poll
 *          (other negative)      transport-layer failure
 *
 *        Frame contents are the full EthII frame (DST MAC, SRC MAC,
 *        EtherType, payload).  The driver prepends SDPCM + BDC
 *        headers.  Caller does not need to add FCS — the chip
 *        computes it on the air.
 */
int whd_tx_eth(const uint8_t *frame, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_DRV_WIFI_CYW43_WHD_H_ */
