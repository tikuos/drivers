/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * whd.c - CYW43439 WHD protocol layer
 *
 * Sits above gspi.c (the transport) and below tiku_drv_wifi_cyw43.c
 * (the driver registration). This file owns:
 *
 *   - the local 2 KB F2 RX buffer
 *   - the SDPCM sequence + credit state (sdpcm_seq, sdpcm_seq_max)
 *   - the IOCTL id counter
 *   - SDPCM/CDC frame build + parse
 *   - the chip's bring-up sequence above the firmware-boot line:
 *     F2 bus enable, GET("ver"), CLM upload, radio defaults + MAC
 *     readback, event-channel reader, and a single active scan.
 *
 * Behaviour is exactly what cyw43_gspi_probe_chip_id() did before
 * the R.1 split — we moved code, did not change it. Phase R.2 will
 * turn whd_init() into a kernel process with a runner loop; this
 * file is the natural home for that.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "whd.h"
#include "gspi.h"
#include "firmware.h"
#include "tiku.h"
#include <arch/arm-rp2350/tiku_uart_arch.h>
#include <interfaces/led/tiku_led.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_timer.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Memory-safety model                                                       */
/*---------------------------------------------------------------------------*/
/*
 * The WHD layer reads bytes that come straight from the air — every
 * scan/event/data frame is attacker-influenceable. Three protections
 * are in place against a corrupt frame causing damage outside our
 * own buffer:
 *
 *  1. Non-execute (NX). The arena lives in TikuOS's SEG2 SRAM region,
 *     which the platform MPU configures as RW + XN. A corrupted frame
 *     can't be executed even if it contained bytes that resemble
 *     instructions.
 *
 *  2. Region registration. tiku_arena_create() claims the backing
 *     buffer in the kernel's region registry, so any region-aware
 *     code can ask "who owns this address?" rather than guessing.
 *
 *  3. Bounded carving. The arena's bump allocator returns aligned
 *     sub-buffers of fixed sizes; the carving itself can't slop into
 *     unrelated memory. (A buffer overrun inside our own code is
 *     still possible — the bounds aren't hardware-enforced. But
 *     that's a code bug, not a chip attack surface, and the W^X
 *     invariant above stops it from escalating to RCE.)
 *
 * What's NOT here: a per-driver MPU region that fences the WHD arena
 * from kernel data. The RP2350 MPU has 8 regions; 6 are already used
 * for system-wide W^X and a stack-overflow guard. Adding a 7th region
 * for the WHD arena is a kernel-side feature, not a driver-side one,
 * and the value-add is small given the W^X invariant already prevents
 * the worst outcomes.
 */

#ifndef CYW43_PRINTF
#define CYW43_PRINTF(...) TIKU_PRINTF("[cyw43] " __VA_ARGS__)
#endif

/*---------------------------------------------------------------------------*/
/* WHD STATE                                                                 */
/*---------------------------------------------------------------------------*/
/*
 * SDPCM credit-based flow control. sdpcm_seq is the TX seq we'll
 * use for our NEXT outbound frame; sdpcm_seq_max is the largest
 * seq value the chip has granted (= chip's bus_data_credit field
 * in the last received SDPCM header).
 *
 * Initial values seq=0, seq_max=1 — first TX is allowed before the
 * chip has granted credit (matches embassy-rs cyw43 runner).
 */
static struct {
    uint8_t  sdpcm_seq;
    uint8_t  sdpcm_seq_max;
    uint16_t ioctl_id;
} whd = {
    .sdpcm_seq     = 0U,
    .sdpcm_seq_max = 1U,
    .ioctl_id      = 0U,
};

/*---------------------------------------------------------------------------*/
/* MEMORY POOL (tiku_arena-backed)                                           */
/*---------------------------------------------------------------------------*/
/*
 * The WHD layer's three persistent buffers — F2 RX (2 KB), TX
 * scratch (2 KB), CLM-upload iovar buf (~1 KB) — are allocated
 * once from a tiku_arena that the kernel registers in its region
 * map. `ps` / region listings now see WiFi's memory budget rather
 * than the buffers being invisible static .bss arrays.
 *
 * Phase 5 (RX ring) will add a tiku_pool of fixed-size packet
 * blocks for incoming frames — the arena handles "alloc once,
 * hold forever" allocations, the pool will handle the per-packet
 * ring with individual free/realloc.
 */
#define WHD_RX_BUF_WORDS     512U                       /* 2 KB */
#define WHD_TX_SCRATCH_BYTES 2048U
#define WHD_CLM_IOV_BYTES    (8U + 12U + 1024U)         /* "clmload" + DownloadHeader + 1 KB chunk */
#define WHD_ARENA_BYTES      (WHD_RX_BUF_WORDS * 4U  \
                              + WHD_TX_SCRATCH_BYTES \
                              + WHD_CLM_IOV_BYTES    \
                              + 64U /* alignment slack */)

static uint8_t       whd_arena_buf[WHD_ARENA_BYTES]
                     __attribute__((aligned(4)));
static tiku_arena_t  whd_arena;

/* Pointers populated by whd_mem_init(); used by the runner + helpers
 * throughout the driver lifetime. */
static uint32_t     *whd_rx_buf;
static uint8_t      *whd_tx_scratch;
static uint8_t      *whd_clm_iov_buf;

/*---------------------------------------------------------------------------*/
/* RUNNER STATE                                                              */
/*---------------------------------------------------------------------------*/
/* Cached snapshot of the chip's bring-up state. Read by anyone via
 * cyw43_wifi_status(); written by the runner protothread. The fields
 * are independent uint reads, so the lack of locking is safe under
 * cooperative scheduling (other processes only run at YIELD points,
 * which the runner doesn't hit while it's updating these). */
static struct {
    uint8_t  up;
    uint8_t  scan_in_progress;
    uint16_t scan_aps_found;
    uint8_t  mac[6];
    /* Wall-clock duration of the LAST completed scan, in TIKU_CLOCK
     * ticks (TIKU_CLOCK_SECOND ticks/sec). 0 if no scan has finished
     * yet. Captured at scan_start / scan_end inside the runner. */
    uint32_t last_scan_ticks;
    /* Last completed scan's deduplicated AP table. The runner
     * appends here during scan; subscribers iterate after the
     * CYW43_WIFI_EVT_SCAN_COMPLETE broadcast. */
    uint8_t    scan_count;
    cyw43_ap_t scan_results[CYW43_MAX_SCAN_RESULTS];
} cyw43_state;

/*---------------------------------------------------------------------------*/
/* SDPCM credit update                                                       */
/*---------------------------------------------------------------------------*/
/* Update sdpcm_seq_max from the credit field of a just-received
 * SDPCM frame. Mirrors embassy's update_credit(). */
static void sdpcm_update_credit_from_rx(const uint8_t *rx_bytes)
{
    uint8_t  ch_flags = rx_bytes[5];
    uint8_t  credit   = rx_bytes[9];
    uint8_t  new_max  = credit;
    /* Only update for valid channels (0..2). channels >= 3 are
     * reserved and don't carry credit info. */
    if ((ch_flags & 0xFU) >= 3U) return;

    /* If chip granted more than 0x40 ahead of our seq, clamp to a
     * sane window (avoid wrap pathology). */
    if ((uint8_t)(new_max - whd.sdpcm_seq) > 0x40U) {
        new_max = (uint8_t)(whd.sdpcm_seq + 2U);
    }
    whd.sdpcm_seq_max = new_max;
}

/*---------------------------------------------------------------------------*/
/* SDPCM + CDC frame builder                                                 */
/*---------------------------------------------------------------------------*/
/* Build a CHANNEL_TYPE_CONTROL SDPCM+CDC frame for an IOCTL.
 * Returns the total wire byte count (already 4-byte aligned).
 * tx_buf must be at least 12 + 16 + payload_len + 3 bytes.
 */
static uint32_t whd_build_ioctl(uint8_t *tx_buf,
                                uint32_t cmd_code, uint16_t kind_flags,
                                uint16_t iface,
                                const uint8_t *payload,
                                uint32_t payload_len,
                                uint16_t this_id)
{
    uint32_t total_len = 12UL + 16UL + payload_len;
    uint16_t inv       = (uint16_t)~(uint16_t)total_len;
    uint16_t cdc_flags = (uint16_t)(kind_flags | (iface << 12));
    uint32_t padded;

    /* SDPCM header. */
    tx_buf[0]  = (uint8_t)(total_len & 0xFFU);
    tx_buf[1]  = (uint8_t)((total_len >> 8) & 0xFFU);
    tx_buf[2]  = (uint8_t)(inv & 0xFFU);
    tx_buf[3]  = (uint8_t)((inv >> 8) & 0xFFU);
    tx_buf[4]  = whd.sdpcm_seq;
    tx_buf[5]  = 0U;  /* channel=0 (CONTROL), flags=0 */
    tx_buf[6]  = 0U;  /* next_length */
    tx_buf[7]  = 12U; /* header_length (= SDPCM size) */
    tx_buf[8]  = 0U;  /* wireless flow control */
    tx_buf[9]  = 0U;  /* bus_data_credit (TX side ignored by chip) */
    tx_buf[10] = 0U;  /* reserved */
    tx_buf[11] = 0U;

    /* CDC header: u32 cmd | u32 len | u16 flags | u16 id | u32 status. */
    tx_buf[12] = (uint8_t)(cmd_code & 0xFFU);
    tx_buf[13] = (uint8_t)((cmd_code >> 8)  & 0xFFU);
    tx_buf[14] = (uint8_t)((cmd_code >> 16) & 0xFFU);
    tx_buf[15] = (uint8_t)((cmd_code >> 24) & 0xFFU);
    tx_buf[16] = (uint8_t)(payload_len & 0xFFU);
    tx_buf[17] = (uint8_t)((payload_len >> 8) & 0xFFU);
    tx_buf[18] = (uint8_t)((payload_len >> 16) & 0xFFU);
    tx_buf[19] = (uint8_t)((payload_len >> 24) & 0xFFU);
    tx_buf[20] = (uint8_t)(cdc_flags & 0xFFU);
    tx_buf[21] = (uint8_t)((cdc_flags >> 8) & 0xFFU);
    tx_buf[22] = (uint8_t)(this_id & 0xFFU);
    tx_buf[23] = (uint8_t)((this_id >> 8) & 0xFFU);
    tx_buf[24] = 0U;  /* status: 0 on TX */
    tx_buf[25] = 0U;
    tx_buf[26] = 0U;
    tx_buf[27] = 0U;

    /* Payload after the headers. */
    {
        uint32_t i;
        for (i = 0U; i < payload_len; ++i) {
            tx_buf[28U + i] = payload[i];
        }
    }

    /* Round up to multiple of 4 bytes; zero-pad the tail. */
    padded = (total_len + 3U) & ~3UL;
    {
        uint32_t i;
        for (i = total_len; i < padded; ++i) {
            tx_buf[i] = 0U;
        }
    }
    return padded;
}

/*---------------------------------------------------------------------------*/
/* IOCTL request/response                                                    */
/*---------------------------------------------------------------------------*/

int whd_ioctl(uint16_t kind_flags, uint32_t cmd_code, uint16_t iface,
              const uint8_t *tx_data, uint32_t tx_len,
              uint8_t *rx_data, uint32_t rx_size,
              uint32_t *rx_len_out)
{
    const uint8_t *rx_bytes = (const uint8_t *)whd_rx_buf;
    uint16_t  this_id;
    uint32_t  frame_bytes;
    uint32_t  rx_pkt_len = 0UL;
    unsigned int tries;
    int rc;

    if (rx_len_out != (uint32_t *)0) *rx_len_out = 0UL;

    if (tx_len + 28U > WHD_TX_SCRATCH_BYTES) {
        return TIKU_DRV_ERR_INVALID;
    }

    whd.ioctl_id = (uint16_t)(whd.ioctl_id + 1U);
    this_id      = whd.ioctl_id;

    frame_bytes = whd_build_ioctl(whd_tx_scratch,
                                  cmd_code, kind_flags, iface,
                                  tx_data, tx_len, this_id);
    whd.sdpcm_seq = (uint8_t)(whd.sdpcm_seq + 1U);

    rc = cyw43_gspi_f2_tx((const uint32_t *)whd_tx_scratch, frame_bytes);
    if (rc != TIKU_DRV_OK) {
        CYW43_PRINTF("whd_ioctl: TX FAIL rc=%d (cmd=%lu)\n",
                     rc, (unsigned long)cmd_code);
        return rc;
    }

    /* 500 polls × 50 ms each = 25 s worst-case budget. Most IOCTLs
     * respond within 1-2 polls; CLM upload chunks can take a bit
     * longer because the chip parses each chunk before acking. */
    for (tries = 0U; tries < 500U; ++tries) {
        rc = cyw43_gspi_f2_rx_wait(whd_rx_buf, WHD_RX_BUF_WORDS,
                                   &rx_pkt_len, 50U);
        if (rc == TIKU_DRV_ERR_TIMEOUT) continue;
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("whd_ioctl: RX FAIL rc=%d\n", rc);
            return rc;
        }
        sdpcm_update_credit_from_rx(rx_bytes);

        {
            uint16_t sdpcm_len = (uint16_t)(rx_bytes[0] | (rx_bytes[1] << 8));
            uint8_t  ch        = rx_bytes[5] & 0xFU;
            uint8_t  hdr_off   = rx_bytes[7];
            const uint8_t *cdc;
            uint16_t cdc_id;
            uint32_t cdc_status, cdc_len;

            if (ch != 0U) {
                /* Event or data frame slipped in; drop and continue. */
                continue;
            }
            if (hdr_off + 16U > sdpcm_len) {
                continue;
            }
            cdc        = rx_bytes + hdr_off;
            cdc_len    =  (uint32_t)cdc[4]        | ((uint32_t)cdc[5]  << 8)
                       | ((uint32_t)cdc[6]  << 16)| ((uint32_t)cdc[7]  << 24);
            cdc_id     = (uint16_t)(cdc[10] | (cdc[11] << 8));
            cdc_status =  (uint32_t)cdc[12]       | ((uint32_t)cdc[13] << 8)
                       | ((uint32_t)cdc[14] << 16)| ((uint32_t)cdc[15] << 24);

            if (cdc_id != this_id) continue;

            if (cdc_status != 0UL) {
                CYW43_PRINTF("whd_ioctl: cmd=%lu IOCTL error 0x%08lx\n",
                             (unsigned long)cmd_code,
                             (unsigned long)cdc_status);
                return TIKU_DRV_ERR_NOT_PRESENT;
            }

            if (rx_data != (uint8_t *)0 && rx_size > 0U) {
                uint32_t avail = (uint32_t)sdpcm_len - hdr_off - 16U;
                uint32_t n     = (cdc_len < avail) ? cdc_len : avail;
                if (n > rx_size) n = rx_size;
                {
                    uint32_t i;
                    for (i = 0U; i < n; ++i) rx_data[i] = cdc[16U + i];
                }
            }
            if (rx_len_out != (uint32_t *)0) *rx_len_out = cdc_len;
            return TIKU_DRV_OK;
        }
    }
    CYW43_PRINTF("whd_ioctl: cmd=%lu id=%u TIMEOUT — no response\n",
                 (unsigned long)cmd_code, this_id);
    return TIKU_DRV_ERR_TIMEOUT;
}

/*---------------------------------------------------------------------------*/
/* whd_bring_up — phases 3.A through 3.E + event_msgs + WLC_UP               */
/*---------------------------------------------------------------------------*/
/*
 * Pre-condition: chip's firmware is running (cyw43_gspi_probe_chip_id
 * returned OK and HT clock came up). This function brings the WHD
 * layer up to "radio is enabled, events are routed, scan/join are
 * ready to call." It is synchronous (busy-waits on chip responses)
 * and is intended to be called from the cyw43_runner protothread's
 * first dispatch.
 *
 * Phase R.3 will swap the internal busy-waits for tiku_timer-based
 * yields so the protothread doesn't monopolise the CPU during the
 * one-time bring-up.
 */
static int whd_bring_up(void)
{
    int rc;

    /*-----------------------------------------------------------*/
    /* PHASE 3.A: enable F2 packet path.                         */
    /*-----------------------------------------------------------*/
    /* Embassy's SPI bus init does two writes here:
     *   1. Enable IRQ_F2_PACKET_AVAILABLE in SPI_INT_EN_REG
     *      (bytes 2..3 of the 32-bit word at F0/0x0004).
     *   2. Write SPI_F2_WATERMARK = 0x20 to F1/REG_F2_WATERMARK
     *      (F1-direct register like CHIP_CLOCK_CSR).
     * Then poll SPI_STATUS until F2_RX_READY flips to 1.
     */
    {
        uint32_t status = 0UL;
        uint8_t  wm_rb  = 0U;
        unsigned int polls;

        rc = cyw43_gspi_write32(GSPI_FUNCTION_BUS,
                                GSPI_REG_SPI_INT_REG,
                                ((uint32_t)GSPI_IRQ_F2_PACKET_AVAILABLE)
                                    << 16);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p3.A: enable F2 IRQ FAIL rc=%d\n", rc);
            return rc;
        }

        rc = cyw43_gspi_write8(GSPI_FUNCTION_BACKPLANE,
                               GSPI_REG_F2_WATERMARK,
                               GSPI_F2_WATERMARK_BYTES);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p3.A: F2 watermark write FAIL rc=%d\n", rc);
            return rc;
        }
        rc = cyw43_gspi_read8(GSPI_FUNCTION_BACKPLANE,
                              GSPI_REG_F2_WATERMARK, &wm_rb);
        if (rc != TIKU_DRV_OK || wm_rb != GSPI_F2_WATERMARK_BYTES) {
            CYW43_PRINTF("p3.A: F2 watermark readback FAIL "
                         "(got 0x%02x, want 0x%02x, rc=%d)\n",
                         wm_rb, GSPI_F2_WATERMARK_BYTES, rc);
            return TIKU_DRV_ERR_NOT_PRESENT;
        }
        CYW43_PRINTF("p3.A: F2 watermark set to 0x%02x (verified)\n",
                     wm_rb);

        for (polls = 0U; polls < 500U; ++polls) {
            tiku_common_delay_ms(2U);
            rc = cyw43_gspi_read32(GSPI_FUNCTION_BUS,
                                   GSPI_REG_SPI_STATUS, &status);
            if (rc != TIKU_DRV_OK) continue;
            if (status & GSPI_STATUS_F2_RX_READY) {
                CYW43_PRINTF("p3.A: F2_RX_READY UP after %u polls "
                             "(STATUS=0x%08lx) *** F2 bus open ***\n",
                             polls, (unsigned long)status);
                break;
            }
        }
        if (!(status & GSPI_STATUS_F2_RX_READY)) {
            CYW43_PRINTF("p3.A: F2_RX_READY TIMEOUT (last STATUS=0x%08lx)\n",
                         (unsigned long)status);
            return TIKU_DRV_ERR_TIMEOUT;
        }

        CYW43_PRINTF("p3.A: *** F2 bus enabled — phase 3.A done ***\n");
    }

    /*-----------------------------------------------------------*/
    /* PHASE 3.B: read one SDPCM frame from F2 (boot-ready event). */
    /*-----------------------------------------------------------*/
    {
        const uint8_t *rx_buf = (const uint8_t *)whd_rx_buf;
        uint32_t  pkt_len = 0UL;
        uint16_t  hdr_len, hdr_len_inv;
        uint8_t   ch_flags, seq, hdr_off, credit;

        rc = cyw43_gspi_f2_rx_try(whd_rx_buf, WHD_RX_BUF_WORDS, &pkt_len);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p3.B: F2 rx FAIL rc=%d\n", rc);
            return rc;
        }
        if (pkt_len == 0U) {
            CYW43_PRINTF("p3.B: no F2 packet available — skipping\n");
            goto p3b_done;
        }

        hdr_len     = (uint16_t)(rx_buf[0] | (rx_buf[1] << 8));
        hdr_len_inv = (uint16_t)(rx_buf[2] | (rx_buf[3] << 8));
        seq         = rx_buf[4];
        ch_flags    = rx_buf[5];
        hdr_off     = rx_buf[7];
        credit      = rx_buf[9];

        CYW43_PRINTF("p3.B: F2 frame got %lu B  hdr_len=%u inv=0x%04x "
                     "(~hdr_len=0x%04x %s)\n",
                     (unsigned long)pkt_len, hdr_len, hdr_len_inv,
                     (uint16_t)(~hdr_len),
                     (hdr_len_inv == (uint16_t)(~hdr_len))
                        ? "OK" : "MISMATCH");
        CYW43_PRINTF("p3.B:   seq=%u  channel=%u  flags=0x%x  "
                     "hdr_off=%u  credit=%u\n",
                     seq, ch_flags & 0xF, (ch_flags >> 4) & 0xF,
                     hdr_off, credit);

        if (hdr_len_inv != (uint16_t)(~hdr_len)
            || hdr_len != (uint16_t)pkt_len) {
            CYW43_PRINTF("p3.B: SDPCM header integrity FAIL\n");
            return TIKU_DRV_ERR_NOT_PRESENT;
        }

        sdpcm_update_credit_from_rx(rx_buf);
        CYW43_PRINTF("p3.B: *** SDPCM frame OK — phase 3.B done "
                     "(seq_max now %u) ***\n", whd.sdpcm_seq_max);
    }
p3b_done:

    /*-----------------------------------------------------------*/
    /* PHASE 3.C: GET("ver") via CDC IOCTL.                      */
    /*-----------------------------------------------------------*/
    {
        uint8_t  iov_buf[128];
        uint32_t resp_len = 0UL;
        uint16_t i;
        const char ver_name[] = "ver";

        for (i = 0U; i < sizeof ver_name; ++i) iov_buf[i] = (uint8_t)ver_name[i];
        for (i = (uint16_t)sizeof ver_name; i < sizeof iov_buf; ++i) {
            iov_buf[i] = 0U;
        }
        rc = whd_ioctl(WHD_IOCTL_KIND_GET, WHD_CMD_GET_VAR, 0U,
                       iov_buf, sizeof iov_buf,
                       iov_buf, sizeof iov_buf, &resp_len);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p3.C: GET(\"ver\") failed rc=%d\n", rc);
            return rc;
        }
        CYW43_PRINTF("p3.C: *** GET(\"ver\") response (%lu B):\n",
                     (unsigned long)resp_len);
        {
            uint32_t n = (resp_len < sizeof iov_buf) ? resp_len : sizeof iov_buf;
            uint32_t j;
            for (j = 0U; j < n; ++j) {
                char c = (char)iov_buf[j];
                if (c == '\0') break;
                if (c == '\n' || c == '\r') {
                    tiku_uart_putc(' ');
                } else if (c >= 0x20 && c < 0x7F) {
                    tiku_uart_putc(c);
                } else {
                    tiku_uart_putc('.');
                }
            }
            tiku_uart_putc('\n');
        }
        CYW43_PRINTF("p3.C: *** phase 3.C done ***\n");
    }

    /*-----------------------------------------------------------*/
    /* PHASE 3.D: upload CLM (Country Locale Matrix) blob.       */
    /*-----------------------------------------------------------*/
    {
        /* Arena-allocated buffer (whd_clm_iov_buf) instead of a
         * static — kernel's region registry tracks the ~1 KB usage. */
        uint8_t        *iov_buf = whd_clm_iov_buf;
        uint8_t         resp_buf[4];
        uint32_t        resp_len = 0UL;
        const uint32_t  clm_size = cyw43_clm_size;
        const uint16_t  flag = 0x0002U | 0x0004U | 0x1000U; /* BEGIN|END|HANDLER_VER */
        const uint16_t  dload_type = 2U; /* CLM */
        uint16_t        i;

        if (clm_size > 1024UL) {
            CYW43_PRINTF("p3.D: CLM too big (%lu B) for single chunk\n",
                         (unsigned long)clm_size);
            return TIKU_DRV_ERR_INVALID;
        }

        iov_buf[0] = 'c'; iov_buf[1] = 'l'; iov_buf[2] = 'm';
        iov_buf[3] = 'l'; iov_buf[4] = 'o'; iov_buf[5] = 'a';
        iov_buf[6] = 'd'; iov_buf[7] = '\0';

        iov_buf[8]  = (uint8_t)(flag & 0xFFU);
        iov_buf[9]  = (uint8_t)((flag >> 8) & 0xFFU);
        iov_buf[10] = (uint8_t)(dload_type & 0xFFU);
        iov_buf[11] = (uint8_t)((dload_type >> 8) & 0xFFU);
        iov_buf[12] = (uint8_t)(clm_size & 0xFFU);
        iov_buf[13] = (uint8_t)((clm_size >> 8) & 0xFFU);
        iov_buf[14] = (uint8_t)((clm_size >> 16) & 0xFFU);
        iov_buf[15] = (uint8_t)((clm_size >> 24) & 0xFFU);
        iov_buf[16] = 0U; iov_buf[17] = 0U;
        iov_buf[18] = 0U; iov_buf[19] = 0U;       /* crc = 0 */

        for (i = 0U; i < clm_size; ++i) {
            iov_buf[20U + i] = cyw43_clm_data[i];
        }

        CYW43_PRINTF("p3.D: uploading CLM (%lu B as single chunk)\n",
                     (unsigned long)clm_size);
        rc = whd_ioctl(WHD_IOCTL_KIND_SET, WHD_CMD_SET_VAR, 0U,
                       iov_buf, 20UL + clm_size,
                       (uint8_t *)0, 0U, (uint32_t *)0);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p3.D: clmload SET FAIL rc=%d\n", rc);
            return rc;
        }
        CYW43_PRINTF("p3.D: clmload SET ok\n");

        iov_buf[0]  = 'c'; iov_buf[1]  = 'l'; iov_buf[2]  = 'm';
        iov_buf[3]  = 'l'; iov_buf[4]  = 'o'; iov_buf[5]  = 'a';
        iov_buf[6]  = 'd'; iov_buf[7]  = '_'; iov_buf[8]  = 's';
        iov_buf[9]  = 't'; iov_buf[10] = 'a'; iov_buf[11] = 't';
        iov_buf[12] = 'u'; iov_buf[13] = 's'; iov_buf[14] = '\0';
        iov_buf[15] = 0U;  iov_buf[16] = 0U;  iov_buf[17] = 0U;
        iov_buf[18] = 0U;
        rc = whd_ioctl(WHD_IOCTL_KIND_GET, WHD_CMD_GET_VAR, 0U,
                       iov_buf, 19U,
                       resp_buf, sizeof resp_buf, &resp_len);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p3.D: clmload_status GET FAIL rc=%d\n", rc);
            return rc;
        }
        {
            uint32_t status_u32 = (uint32_t)resp_buf[0]
                                  | ((uint32_t)resp_buf[1] << 8)
                                  | ((uint32_t)resp_buf[2] << 16)
                                  | ((uint32_t)resp_buf[3] << 24);
            CYW43_PRINTF("p3.D: clmload_status = 0x%08lx (len=%lu)\n",
                         (unsigned long)status_u32,
                         (unsigned long)resp_len);
            if (status_u32 != 0UL) {
                CYW43_PRINTF("p3.D: CLM not accepted by chip\n");
                return TIKU_DRV_ERR_NOT_PRESENT;
            }
        }
        CYW43_PRINTF("p3.D: *** CLM uploaded — phase 3.D done ***\n");
    }

    /*-----------------------------------------------------------*/
    /* PHASE 3.E: bring-up IOCTLs + MAC readback.                */
    /*-----------------------------------------------------------*/
    {
        uint8_t  iov_buf[32];
        uint32_t resp_len = 0UL;
        const char *name;
        uint16_t i, name_len;

        name = "bus:txglom";
        name_len = (uint16_t)(strlen(name) + 1U);
        for (i = 0U; i < name_len - 1U; ++i) iov_buf[i] = (uint8_t)name[i];
        iov_buf[name_len - 1U] = 0U;
        iov_buf[name_len + 0U] = 0U;   /* value u32 = 0 */
        iov_buf[name_len + 1U] = 0U;
        iov_buf[name_len + 2U] = 0U;
        iov_buf[name_len + 3U] = 0U;
        rc = whd_ioctl(WHD_IOCTL_KIND_SET, WHD_CMD_SET_VAR, 0U,
                       iov_buf, (uint32_t)(name_len + 4U),
                       (uint8_t *)0, 0U, (uint32_t *)0);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p3.E: SET(\"bus:txglom\", 0) FAIL rc=%d\n", rc);
            return rc;
        }
        CYW43_PRINTF("p3.E: SET(\"bus:txglom\", 0) ok\n");

        name = "apsta";
        name_len = (uint16_t)(strlen(name) + 1U);
        for (i = 0U; i < name_len - 1U; ++i) iov_buf[i] = (uint8_t)name[i];
        iov_buf[name_len - 1U] = 0U;
        iov_buf[name_len + 0U] = 1U;   /* value u32 = 1 */
        iov_buf[name_len + 1U] = 0U;
        iov_buf[name_len + 2U] = 0U;
        iov_buf[name_len + 3U] = 0U;
        rc = whd_ioctl(WHD_IOCTL_KIND_SET, WHD_CMD_SET_VAR, 0U,
                       iov_buf, (uint32_t)(name_len + 4U),
                       (uint8_t *)0, 0U, (uint32_t *)0);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p3.E: SET(\"apsta\", 1) FAIL rc=%d\n", rc);
            return rc;
        }
        CYW43_PRINTF("p3.E: SET(\"apsta\", 1) ok\n");

        name = "cur_etheraddr";
        name_len = (uint16_t)(strlen(name) + 1U);
        for (i = 0U; i < name_len - 1U; ++i) iov_buf[i] = (uint8_t)name[i];
        iov_buf[name_len - 1U] = 0U;
        for (i = name_len; i < name_len + 8U; ++i) iov_buf[i] = 0U;
        rc = whd_ioctl(WHD_IOCTL_KIND_GET, WHD_CMD_GET_VAR, 0U,
                       iov_buf, (uint32_t)(name_len + 8U),
                       iov_buf, sizeof iov_buf, &resp_len);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p3.E: GET(\"cur_etheraddr\") FAIL rc=%d\n", rc);
            return rc;
        }
        if (resp_len < 6U) {
            CYW43_PRINTF("p3.E: MAC reply too short (%lu B)\n",
                         (unsigned long)resp_len);
            return TIKU_DRV_ERR_NOT_PRESENT;
        }
        {
            int j;
            for (j = 0; j < 6; ++j) cyw43_state.mac[j] = iov_buf[j];
        }
        CYW43_PRINTF("p3.E: *** MAC = %02x:%02x:%02x:%02x:%02x:%02x "
                     "(phase 3.E done) ***\n",
                     iov_buf[0], iov_buf[1], iov_buf[2],
                     iov_buf[3], iov_buf[4], iov_buf[5]);
    }

    /*-----------------------------------------------------------*/
    /* event_msgs SET + WLC_UP — radio is now scannable.         */
    /*-----------------------------------------------------------*/
    /* The old phase 3.F also drained a few events to verify the
     * parser; that test moved to a side-effect of whd_scan_once()
     * (every scan emits a burst of events). */
    {
        static const char ev_name[] = "event_msgs";
        uint8_t  ev_buf[sizeof ev_name + 24U];
        uint16_t i;
        for (i = 0U; i < sizeof ev_name; ++i) ev_buf[i] = (uint8_t)ev_name[i];
        for (i = 0U; i < 24U; ++i) ev_buf[sizeof ev_name + i] = 0xFFU;
        rc = whd_ioctl(WHD_IOCTL_KIND_SET, WHD_CMD_SET_VAR, 0U,
                       ev_buf, sizeof ev_buf,
                       (uint8_t *)0, 0U, (uint32_t *)0);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("bring_up: SET(\"event_msgs\") FAIL rc=%d\n", rc);
            return rc;
        }
        CYW43_PRINTF("bring_up: event_msgs enabled\n");
    }

    rc = whd_ioctl(WHD_IOCTL_KIND_SET, WHD_CMD_UP, 0U,
                   (const uint8_t *)0, 0U,
                   (uint8_t *)0, 0U, (uint32_t *)0);
    if (rc != TIKU_DRV_OK) {
        CYW43_PRINTF("bring_up: WLC_UP failed rc=%d\n", rc);
        return rc;
    }
    CYW43_PRINTF("bring_up: WLC_UP ok — *** WHD ready ***\n");

    return TIKU_DRV_OK;
}

/*---------------------------------------------------------------------------*/
/* Scan helpers — split into iovar-send + per-frame parser                   */
/*---------------------------------------------------------------------------*/
/*
 * Phase R.3 moved the drain loop out of this file into the runner
 * protothread (whd.c::cyw43_runner) so the runner can yield to the
 * scheduler between F2 polls instead of busy-waiting. These two
 * helpers are the synchronous building blocks the runner uses.
 */

/* Build + send the "escan" SetVar IOCTL. Returns TIKU_DRV_OK on
 * the chip's ack; chip then drips ESCAN_RESULT events on channel 1
 * until the scan completes (status=0). */
static int whd_scan_send_iovar(void)
{
    static const char escan_name[] = "escan";
    static uint8_t    scan_iov[sizeof escan_name + 76U];
    uint16_t off = 0U;
    uint16_t i;
    int      rc;

    for (i = 0U; i < sizeof escan_name; ++i) scan_iov[off++] = (uint8_t)escan_name[i];

    /* ScanParams (76 B): version=1, action=1, sync_id=1, ssid_len=0,
     * ssid=zeros, bssid=ff..ff, bss_type=2, scan_type=0 (active),
     * nprobes/active/passive/home_time = -1, channel_num=0,
     * channel_list=[0]. */
    scan_iov[off++] = 1U; scan_iov[off++] = 0U;
    scan_iov[off++] = 0U; scan_iov[off++] = 0U;
    scan_iov[off++] = 1U; scan_iov[off++] = 0U;
    scan_iov[off++] = 1U; scan_iov[off++] = 0U;

    for (i = 0U; i < 4U + 32U; ++i) scan_iov[off++] = 0U;
    for (i = 0U; i < 6U; ++i)      scan_iov[off++] = 0xFFU;
    scan_iov[off++] = 2U;
    scan_iov[off++] = 0U;
    for (i = 0U; i < 16U; ++i)     scan_iov[off++] = 0xFFU;
    for (i = 0U; i < 6U; ++i)      scan_iov[off++] = 0U;

    CYW43_PRINTF("p4.A: triggering active scan (all channels, %u-byte "
                 "iovar)\n", off);
    rc = whd_ioctl(WHD_IOCTL_KIND_SET, WHD_CMD_SET_VAR, 0U,
                   scan_iov, off,
                   (uint8_t *)0, 0U, (uint32_t *)0);
    if (rc != TIKU_DRV_OK) {
        CYW43_PRINTF("p4.A: escan SET FAIL rc=%d\n", rc);
    } else {
        CYW43_PRINTF("p4.A: scan started, listening for results...\n");
    }
    return rc;
}

/* Parse the frame currently sitting in whd_rx_buf. Return value:
 *   +1  scan-complete event (status=0): runner can stop polling
 *    0  AP discovered (printed inline); caller may want to reset
 *       its poll-budget after this
 *   -1  unrelated/incomplete frame; caller should keep draining
 */
static int whd_scan_process_frame(unsigned int *aps_seen_inout)
{
    const uint8_t *rx_buf = (const uint8_t *)whd_rx_buf;
    uint8_t  ch       = rx_buf[5] & 0xFU;
    uint16_t sdpcm_lc = (uint16_t)(rx_buf[0] | (rx_buf[1] << 8));
    uint8_t  hdr_off  = rx_buf[7];
    const uint8_t *bdc, *eth, *evh, *msg, *event_data;
    uint16_t ether_type, evh_user_sub;
    uint32_t ev_type, ev_status, ev_datalen;

    if (ch != 1U) return -1;
    if ((uint32_t)hdr_off + 4U + 14U + 10U + 48U > sdpcm_lc) return -1;

    bdc          = rx_buf + hdr_off;
    eth          = bdc + 4U + ((uint32_t)bdc[3] * 4U);
    ether_type   = (uint16_t)((eth[12] << 8) | eth[13]);
    evh          = eth + 14U;
    evh_user_sub = (uint16_t)((evh[8] << 8) | evh[9]);
    msg          = evh + 10U;
    ev_type      =  ((uint32_t)msg[4]  << 24) | ((uint32_t)msg[5]  << 16)
                 |  ((uint32_t)msg[6]  <<  8) |  (uint32_t)msg[7];
    ev_status    =  ((uint32_t)msg[8]  << 24) | ((uint32_t)msg[9]  << 16)
                 |  ((uint32_t)msg[10] <<  8) |  (uint32_t)msg[11];
    ev_datalen   =  ((uint32_t)msg[20] << 24) | ((uint32_t)msg[21] << 16)
                 |  ((uint32_t)msg[22] <<  8) |  (uint32_t)msg[23];
    event_data   = msg + 48U;

    if (ether_type != 0x886CU || evh_user_sub != 1U) return -1;
    if (ev_type != 69U) return -1;

    if (ev_status == 0U) {
        CYW43_PRINTF("p4.A: scan-complete event received (datalen=%lu)\n",
                     (unsigned long)ev_datalen);
        return 1;
    }
    if (ev_status != 8U) {
        CYW43_PRINTF("p4.A: ESCAN status=%lu (not PARTIAL) — skip\n",
                     (unsigned long)ev_status);
        return -1;
    }
    if (ev_datalen < 12U + 80U) {
        CYW43_PRINTF("p4.A: ESCAN partial event too short (datalen=%lu)\n",
                     (unsigned long)ev_datalen);
        return -1;
    }

    {
        const uint8_t *bss     = event_data + 12U;
        const uint8_t *bssid   = bss + 8U;
        uint8_t        ssid_l  = bss[18];
        const uint8_t *ssid    = bss + 19U;
        uint16_t       chanspec = (uint16_t)(bss[72] | (bss[73] << 8));
        int16_t        rssi    = (int16_t)((uint16_t)bss[78]
                                          | ((uint16_t)bss[79] << 8));
        uint16_t       i;
        cyw43_ap_t    *slot     = (cyw43_ap_t *)0;
        int            dup_idx  = -1;

        *aps_seen_inout += 1U;

        /* Dedup by BSSID against the running scan table. If the same
         * BSSID already has an entry, update RSSI if the new one is
         * stronger and skip the append (avoids the 8x repeats of the
         * dominant AP we used to print). */
        if (ssid_l > 32U) ssid_l = 32U;
        for (i = 0U; i < cyw43_state.scan_count; ++i) {
            int match = 1;
            uint8_t k;
            for (k = 0U; k < 6U; ++k) {
                if (cyw43_state.scan_results[i].bssid[k] != bssid[k]) {
                    match = 0;
                    break;
                }
            }
            if (match) { dup_idx = (int)i; break; }
        }

        if (dup_idx >= 0) {
            slot = &cyw43_state.scan_results[dup_idx];
            if (rssi > slot->rssi) slot->rssi = rssi;
        } else if (cyw43_state.scan_count < CYW43_MAX_SCAN_RESULTS) {
            slot = &cyw43_state.scan_results[cyw43_state.scan_count++];
            slot->ssid_len = ssid_l;
            for (i = 0U; i < ssid_l; ++i) slot->ssid[i] = ssid[i];
            for (i = ssid_l; i < 32U; ++i) slot->ssid[i] = 0U;
            for (i = 0U; i < 6U; ++i) slot->bssid[i] = bssid[i];
            slot->rssi    = rssi;
            slot->channel = (uint8_t)(chanspec & 0xFFU);
            slot->_pad    = 0U;
        }

        /* Inline log + AP_FOUND broadcast on the FIRST sighting only. */
        if (dup_idx < 0 && slot != (cyw43_ap_t *)0) {
            CYW43_PRINTF("p4.A: AP #%u  BSSID=%02x:%02x:%02x:%02x:%02x:%02x "
                         " RSSI=%d  ch=%u  SSID=",
                         cyw43_state.scan_count,
                         bssid[0], bssid[1], bssid[2],
                         bssid[3], bssid[4], bssid[5],
                         (int)rssi, chanspec & 0xFFU);
            for (i = 0U; i < ssid_l; ++i) {
                char c = (char)ssid[i];
                if (c >= 0x20 && c < 0x7F) tiku_uart_putc(c);
                else                       tiku_uart_putc('.');
            }
            tiku_uart_putc('\n');

            /* Fan out the per-AP discovery event so subscribers (shell
             * scan command, future IP-config logic, etc.) can react.
             * data carries a pointer to the just-added scan_results
             * entry; the pointer is stable until the next scan starts. */
            (void)tiku_process_post(TIKU_PROCESS_BROADCAST,
                                    CYW43_WIFI_EVT_AP_FOUND, slot);
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* RUNNER PROCESS                                                            */
/*---------------------------------------------------------------------------*/
/*
 * cyw43_runner is the TikuOS process that owns the WHD layer. Its
 * first dispatch runs whd_bring_up() synchronously (busy-wait on
 * chip responses); after that the protothread sits in an event
 * loop, doing scans / future joins on demand.
 *
 * Why a process rather than inline driver init: it lets the rest
 * of the system address the WHD layer asynchronously (post a
 * CYW43_WIFI_EVT_* event, get a result later), and gives us a
 * natural home for phase R.3's timer-yield-based delays + phase
 * R.6's GPIO-IRQ-driven wake.
 */
TIKU_PROCESS(cyw43_runner, "wifi-cyw43");

/* Sample MPU violation count and log if it moved since last check.
 * Called from the runner; non-invasive (just reads + comparing).
 * The MPU is enforced kernel-wide, so any change in the count tells
 * us SOMETHING in the system tripped W^X or wrote to a read-only
 * region — not necessarily WHD code, but worth flagging. */
static uint32_t runner_mpu_baseline;

static void runner_health_tick(void)
{
    /* Kick the watchdog. If WHD wedges and the runner stops yielding,
     * the watchdog bites; the system reboots cleanly instead of
     * sitting locked-up. tiku_watchdog_kick is a no-op if the WDT
     * isn't enabled — safe to call unconditionally. */
    tiku_watchdog_kick();

    /* MPU violation count is a monotonically increasing counter.
     * A jump means at least one MemManage fault was caught (and the
     * platform's MemManage handler likely already reset the chip,
     * but the count survives in .uninit). */
    {
        uint32_t now = tiku_mpu_get_violation_count();
        if (now != runner_mpu_baseline) {
            uint16_t flags = tiku_mpu_get_violation_flags();
            CYW43_PRINTF("runner: MPU violation count %lu -> %lu "
                         "(flags=0x%04x)\n",
                         (unsigned long)runner_mpu_baseline,
                         (unsigned long)now, flags);
            tiku_mpu_clear_violation_flags();
            runner_mpu_baseline = now;
        }
    }
}

TIKU_PROCESS_THREAD(cyw43_runner, ev, data)
{
    /* Static locals survive across YIELD points (auto vars wouldn't —
     * protothreads use a line-numbered case/jump state machine). */
    static struct tiku_timer scan_timer;
    static unsigned int      scan_polls;
    static unsigned int      scan_aps;
    static int               scan_done;
    static int               scan_rc;
    static tiku_clock_time_t scan_start_tick;

    TIKU_PROCESS_BEGIN();

    (void)data;
    runner_mpu_baseline = tiku_mpu_get_violation_count();

    /* LED 0 (board-defined; index 0 is the heartbeat LED on the
     * Pi Pico 2 W external LED pad). Initialised here so the runner
     * can blink it during scans / leave it solid on link-up. If the
     * board has no LED, tiku_led_* is a no-op. */
    if (tiku_led_count() > 0U) {
        tiku_led_init(0U);
        tiku_led_off(0U);
    }

    CYW43_PRINTF("runner: starting WHD bring-up\n");
    if (whd_bring_up() == TIKU_DRV_OK) {
        cyw43_state.up = 1U;
        /* Solid LED = WHD ready. */
        if (tiku_led_count() > 0U) tiku_led_on(0U);
        /* Self-post the first SCAN_START to demonstrate the event
         * path. Once a shell command + user-facing API land (phase
         * R.5), this auto-scan can be removed. */
        (void)tiku_process_post(&cyw43_runner,
                                CYW43_WIFI_EVT_SCAN_START, NULL);
    } else {
        CYW43_PRINTF("runner: WHD bring-up failed — runner idle\n");
    }

    while (1) {
        TIKU_PROCESS_YIELD();

        /* Runner-level health tick: kick the watchdog and notice
         * any MPU violation that may have happened anywhere in the
         * system since the last check. Cheap (two reads + a write),
         * keeps the WiFi side observably alive. */
        runner_health_tick();

        if (ev == CYW43_WIFI_EVT_SCAN_START && cyw43_state.up) {
            CYW43_PRINTF("runner: SCAN_START — starting active scan\n");
            cyw43_state.scan_in_progress = 1U;
            cyw43_state.scan_count       = 0U;  /* clear table for fresh scan */
            scan_start_tick              = tiku_clock_time();

            scan_rc = whd_scan_send_iovar();
            if (scan_rc != TIKU_DRV_OK) {
                cyw43_state.scan_in_progress = 0U;
                continue;
            }

            scan_polls = 0U;
            scan_aps   = 0U;
            scan_done  = 0;

            /* Phase R.3: yield 1 tick (~7.81 ms) every iteration —
             * even when an AP just arrived — so the scheduler can
             * run the shell / other processes between events. The
             * chip sometimes pumps several frames in tight bursts
             * during a busy channel; without an unconditional yield
             * we'd process all of them back-to-back and starve
             * other processes for hundreds of ms.
             *
             * Cost: ~7.81 ms per AP found adds ~80 ms to a typical
             * 10-AP scan (~2% extra latency), in exchange for the
             * shell staying responsive throughout. */
            while (!scan_done && scan_polls < 700U) {
                uint32_t pkt_len = 0UL;
                int      pr;

                /* Scan can take seconds; keep kicking the watchdog
                 * so it doesn't bite mid-scan. (The runner DOES
                 * yield each iteration, but watchdog timing is
                 * independent of yields.) */
                tiku_watchdog_kick();

                (void)cyw43_gspi_f2_rx_try(whd_rx_buf,
                                           WHD_RX_BUF_WORDS, &pkt_len);
                if (pkt_len > 0U) {
                    sdpcm_update_credit_from_rx(
                        (const uint8_t *)whd_rx_buf);
                    pr = whd_scan_process_frame(&scan_aps);
                    if (pr > 0) {
                        scan_done = 1;
                        break;
                    }
                    if (pr == 0) {
                        /* AP printed — reset the budget. */
                        scan_polls = 0U;
                    }
                } else {
                    scan_polls += 1U;
                }

                PT_WAIT_UNTIL_TIMEOUT(process_pt, &scan_timer,
                                      scan_done, 1U);

                /* Visible scan-in-progress indication: blink the LED
                 * each iteration. With the 1-tick yield above this is
                 * roughly 128 toggles/sec = 64 Hz square wave — fast
                 * enough to look like "solid + flickering," dim enough
                 * to be visibly different from solid-on. */
                if (tiku_led_count() > 0U) tiku_led_toggle(0U);
            }

            /* Scan done — restore LED to solid (WiFi-up indicator). */
            if (tiku_led_count() > 0U) tiku_led_on(0U);

            {
                tiku_clock_time_t elapsed =
                    (tiku_clock_time_t)(tiku_clock_time() - scan_start_tick);
                /* tiku_clock_time_t may be uint16 (MSP430); widen here. */
                cyw43_state.last_scan_ticks = (uint32_t)elapsed;

                if (!scan_done) {
                    CYW43_PRINTF("p4.A: scan poll budget exhausted "
                                 "(%u AP%s found, no scan-complete event,"
                                 " %lu ticks)\n",
                                 scan_aps, scan_aps == 1U ? "" : "s",
                                 (unsigned long)elapsed);
                } else {
                    CYW43_PRINTF("p4.A: *** scan done — %u AP%s in "
                                 "%lu ticks (~%lu ms) ***\n",
                                 scan_aps, scan_aps == 1U ? "" : "s",
                                 (unsigned long)elapsed,
                                 (unsigned long)(((uint32_t)elapsed * 1000UL)
                                                 / TIKU_CLOCK_SECOND));
                }
            }

            cyw43_state.scan_aps_found   = (uint16_t)scan_aps;
            cyw43_state.scan_in_progress = 0U;

            /* Fan out scan-complete to anyone subscribed. Payload =
             * the deduplicated AP count (= scan_count), squeezed
             * into the event-data pointer slot. Subscribers can
             * also call cyw43_wifi_scan_results() to pull the
             * actual table. */
            (void)tiku_process_post(
                TIKU_PROCESS_BROADCAST,
                CYW43_WIFI_EVT_SCAN_COMPLETE,
                (tiku_event_data_t)(uintptr_t)cyw43_state.scan_count);
        }
        /* Future: CYW43_WIFI_EVT_JOIN_START, _DISCONNECT, etc. */
    }

    TIKU_PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

/* Initialise the arena and carve out the three persistent buffers.
 * Idempotent (re-init re-uses the same arena_buf). */
static int whd_mem_init(void)
{
    tiku_mem_err_t err;

    err = tiku_arena_create(&whd_arena, whd_arena_buf,
                            (tiku_mem_arch_size_t)sizeof whd_arena_buf,
                            /* id */ 0xC4U);
    if (err != TIKU_MEM_OK) {
        CYW43_PRINTF("whd_mem_init: tiku_arena_create err=%d\n", err);
        return TIKU_DRV_ERR_NOT_PRESENT;
    }

    whd_rx_buf      = (uint32_t *)tiku_arena_alloc(&whd_arena,
                          WHD_RX_BUF_WORDS * 4U);
    whd_tx_scratch  = (uint8_t  *)tiku_arena_alloc(&whd_arena,
                          WHD_TX_SCRATCH_BYTES);
    whd_clm_iov_buf = (uint8_t  *)tiku_arena_alloc(&whd_arena,
                          WHD_CLM_IOV_BYTES);

    if (whd_rx_buf == (uint32_t *)0
        || whd_tx_scratch  == (uint8_t *)0
        || whd_clm_iov_buf == (uint8_t *)0) {
        CYW43_PRINTF("whd_mem_init: arena alloc FAIL "
                     "(rx=%p tx=%p clm=%p)\n",
                     (void *)whd_rx_buf,
                     (void *)whd_tx_scratch,
                     (void *)whd_clm_iov_buf);
        /* DRV-layer doesn't have a NOMEM code; INVALID is the closest
         * "we asked for something the system can't give us" signal. */
        return TIKU_DRV_ERR_INVALID;
    }

    {
        tiku_mem_stats_t s;
        if (tiku_arena_stats(&whd_arena, &s) == TIKU_MEM_OK) {
            CYW43_PRINTF("whd_mem_init: arena id=0xC4  "
                         "used=%lu/%lu B  peak=%lu  allocs=%lu\n",
                         (unsigned long)s.used_bytes,
                         (unsigned long)s.total_bytes,
                         (unsigned long)s.peak_bytes,
                         (unsigned long)s.alloc_count);
        }
    }
    return TIKU_DRV_OK;
}

int whd_runner_init(void)
{
    int rc = whd_mem_init();
    if (rc != TIKU_DRV_OK) {
        return rc;
    }
    /* Report SRAM footprint to the kernel so `ps` and /proc/<pid>/
     * see the WiFi driver's actual memory budget rather than 0.
     * The big consumer is the arena backing buffer; cyw43_state and
     * the small whd state struct round out the visible cost. The
     * field is uint16_t so we cap at 0xFFFF — our usage (~6 KB) fits. */
    {
        uint32_t total = (uint32_t)sizeof whd_arena_buf
                       + (uint32_t)sizeof cyw43_state
                       + (uint32_t)sizeof whd;
        cyw43_runner.sram_used = (uint16_t)(total > 0xFFFFUL
                                            ? 0xFFFFU : total);
    }
    /* Register with the kernel's process registry (gives the runner a
     * pid + makes it visible in `ps` / /proc/<pid>/). Distinct from
     * tiku_process_start, which only schedules without registering.
     * Re-registration is harmless; we tolerate a non-zero error code
     * silently. */
    (void)tiku_process_register("wifi-cyw43", &cyw43_runner);
    return TIKU_DRV_OK;
}

int tiku_wireless_scan_start(void)
{
    if (!cyw43_state.up) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (cyw43_state.scan_in_progress) {
        return TIKU_DRV_ERR_TIMEOUT;
    }
    return tiku_process_post(&cyw43_runner,
                             CYW43_WIFI_EVT_SCAN_START, NULL)
           ? TIKU_DRV_OK : TIKU_DRV_ERR_TIMEOUT;
}

int tiku_wireless_status(cyw43_wifi_status_t *out)
{
    int i;
    if (out == (cyw43_wifi_status_t *)0) {
        return TIKU_DRV_ERR_INVALID;
    }
    out->up               = cyw43_state.up;
    out->scan_in_progress = cyw43_state.scan_in_progress;
    out->scan_aps_found   = cyw43_state.scan_aps_found;
    out->last_scan_ticks  = cyw43_state.last_scan_ticks;
    for (i = 0; i < 6; ++i) out->mac[i] = cyw43_state.mac[i];
    return TIKU_DRV_OK;
}

uint8_t tiku_wireless_scan_results(cyw43_ap_t *out, uint8_t max_results)
{
    uint8_t i;
    uint8_t n;
    if (out == (cyw43_ap_t *)0 || max_results == 0U) return 0U;
    n = cyw43_state.scan_count;
    if (n > max_results) n = max_results;
    for (i = 0U; i < n; ++i) out[i] = cyw43_state.scan_results[i];
    return n;
}
