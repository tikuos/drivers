/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * bt_transport.c — CYW43439 BTSDIO transport
 *
 * Implements @ref tiku_bt_transport_t for the CYW43439 part. The
 * chip exposes BT/HCI traffic through four circular buffers in
 * WLAN RAM (BTSDIO), accessed via the gSPI backplane window. This
 * file owns:
 *
 *   - BTFW Intel-HEX-flavoured firmware upload
 *   - chip-side BT power-up + FW_RDY/BT_AWAKE polling
 *   - SW_RDY + DATA_VALID handshake
 *   - host-side ring pointer management (H2BT_IN/OUT + BT2H_IN/OUT)
 *   - per-packet BTSDIO encode/decode (4-byte header + padded payload)
 *
 * The BLE protocol stack on top (HCI / L2CAP / ATT / GATT / GAP /
 * SMP) is driver-independent and lives in tikukits/net/bluetooth/.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bt_transport.h"
#include "gspi.h"
#include "firmware.h"
#include "tiku.h"
#include <interfaces/bluetooth/tiku_bt.h>
#include <interfaces/bluetooth/tiku_bt_transport.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <kernel/memory/tiku_mem.h>

#ifndef TIKU_BT_TX_PRINTF
#define TIKU_BT_TX_PRINTF(...) TIKU_PRINTF("[cyw43-bt] " __VA_ARGS__)
#endif

/*---------------------------------------------------------------------------*/
/* CHIP REGISTERS + LAYOUT (per embassy/cyw43/src/consts.rs)                 */
/*---------------------------------------------------------------------------*/

/** Base of the BT subsystem's RAM in the chip address space. */
#define BT_BASE_ADDR                0x19000000UL

/** Power-up request register inside BT subsystem (offset 0x640894). */
#define BT2WLAN_PWRUP_OFFSET        0x00640894UL
#define BT2WLAN_PWRUP_WAKE          0x00000002UL

/** Status registers in the WLAN-side memory map. */
#define BT_CTRL_REG_ADDR            0x18000c7cUL  /* chip writes here */
#define HOST_CTRL_REG_ADDR          0x18000d6cUL  /* host writes here */
#define WLAN_RAM_BASE_REG_ADDR      0x18000d68UL  /* points at ring base */

/** Bit assignments in BT_CTRL_REG / HOST_CTRL_REG. */
#define BTSDIO_REG_DATA_VALID_BIT   (1UL <<  1)
#define BTSDIO_REG_BT_AWAKE_BIT     (1UL <<  8)
#define BTSDIO_REG_WAKE_BT_BIT      (1UL << 17)
#define BTSDIO_REG_SW_RDY_BIT       (1UL << 24)
#define BTSDIO_REG_FW_RDY_BIT       (1UL << 24)

/** Ring buffer layout (offsets from WLAN_RAM_BASE).
 *
 *   0x0000..0x0FFF   host-write buffer (host -> chip)
 *   0x1000..0x1FFF   host-read  buffer (chip -> host)
 *   0x2000..0x2003   H2BT_IN  host's write pointer into H2BT
 *   0x2004..0x2007   H2BT_OUT chip's read  pointer into H2BT
 *   0x2008..0x200B   BT2H_IN  chip's write pointer into BT2H
 *   0x200C..0x200F   BT2H_OUT host's read  pointer into BT2H
 */
#define BTSDIO_FWBUF_SIZE           0x1000UL
#define BTSDIO_OFFSET_HOST_WRITE    0x0000UL
#define BTSDIO_OFFSET_HOST_READ     BTSDIO_FWBUF_SIZE
#define BTSDIO_OFFSET_H2BT_IN       0x2000UL
#define BTSDIO_OFFSET_H2BT_OUT      0x2004UL
#define BTSDIO_OFFSET_BT2H_IN       0x2008UL
#define BTSDIO_OFFSET_BT2H_OUT      0x200CUL

/** BTFW record types (Intel-HEX-flavoured). */
#define BTFW_HEX_LINE_DATA              0U
#define BTFW_HEX_LINE_END_OF_DATA       1U
#define BTFW_HEX_LINE_EXT_SEG_ADDR      2U
#define BTFW_HEX_LINE_EXT_ADDR          4U
#define BTFW_HEX_LINE_ABS_32BIT_ADDR    5U

/** Address-mode state for the BTFW parser. */
#define BTFW_ADDR_MODE_UNKNOWN          0
#define BTFW_ADDR_MODE_EXTENDED         1
#define BTFW_ADDR_MODE_SEGMENT          2
#define BTFW_ADDR_MODE_LINEAR32         3

/*---------------------------------------------------------------------------*/
/* Transport state                                                           */
/*---------------------------------------------------------------------------*/

static struct {
    uint8_t  fw_ready;      /* 1 after FW_RDY observed */
    uint32_t ring_base;     /* from WLAN_RAM_BASE_REG_ADDR */
    uint32_t h2b_write_ptr; /* host's write ptr into H2BT ring */
    uint32_t b2h_read_ptr;  /* host's read  ptr into BT2H ring */
    char     fw_version[96];/* NUL-terminated BTFW header string */
} tx_state;

/** Staging buffer for BTSDIO ring writes/reads. Single-context use
 *  (cooperative scheduler; only one BT send / recv flight at a time).
 *
 *  Allocated from a tiku_arena (id=0xB2) at cyw43_bt_init() time so
 *  the kernel's region map can attribute the buffer to the CYW43 BT
 *  driver. id=0xB1 is the generic stack's command-scratch arena;
 *  distinct ids keep them separable in /proc memory views. */
#define BT_TX_SCRATCH_SIZE   260U
#define BT_TX_ARENA_BYTES    (BT_TX_SCRATCH_SIZE + 32U /* alignment slack */)

static uint8_t      bt_tx_arena_buf[BT_TX_ARENA_BYTES]
                    __attribute__((aligned(4)));
static tiku_arena_t bt_tx_arena;
static uint8_t     *tx_scratch;

/*---------------------------------------------------------------------------*/
/* BTFW parser — Intel-HEX-flavoured firmware records                        */
/*---------------------------------------------------------------------------*/

typedef struct {
    const uint8_t *p;
    uint32_t       remaining;
} btfw_cursor_t;

typedef struct {
    int      addr_mode;
    uint16_t hi_addr;
    uint32_t abs_base_addr32;
    uint32_t dest_addr;
    uint8_t  data[256];
} btfw_state_t;

/**
 * @brief Read one BTFW record into @p st until a DATA record arrives
 *
 * Skips intermediate non-DATA records (EXT_ADDR / EXT_SEG_ADDR /
 * ABS_32BIT_ADDR) which only update the parser's address state.
 *
 * @return Number of data bytes in st->data on a DATA hit, 0 on EOF
 */
static uint32_t btfw_next_data_record(btfw_cursor_t *cur, btfw_state_t *st)
{
    while (cur->remaining >= 4U) {
        uint8_t  num_bytes = cur->p[0];
        uint16_t addr      = (uint16_t)((cur->p[1] << 8) | cur->p[2]);
        uint8_t  line_type = cur->p[3];
        cur->p         += 4;
        cur->remaining -= 4U;

        if (num_bytes == 0U) return 0U;
        if (cur->remaining < num_bytes) return 0U;

        {
            uint8_t i;
            for (i = 0U; i < num_bytes; ++i) st->data[i] = cur->p[i];
        }
        cur->p         += num_bytes;
        cur->remaining -= num_bytes;

        switch (line_type) {
        case BTFW_HEX_LINE_EXT_ADDR:
            st->hi_addr   = (uint16_t)((st->data[0] << 8) | st->data[1]);
            st->addr_mode = BTFW_ADDR_MODE_EXTENDED;
            break;
        case BTFW_HEX_LINE_EXT_SEG_ADDR:
            st->hi_addr   = (uint16_t)((st->data[0] << 8) | st->data[1]);
            st->addr_mode = BTFW_ADDR_MODE_SEGMENT;
            break;
        case BTFW_HEX_LINE_ABS_32BIT_ADDR:
            st->abs_base_addr32 = ((uint32_t)st->data[0] << 24)
                                | ((uint32_t)st->data[1] << 16)
                                | ((uint32_t)st->data[2] <<  8)
                                | ((uint32_t)st->data[3]);
            st->addr_mode = BTFW_ADDR_MODE_LINEAR32;
            break;
        case BTFW_HEX_LINE_DATA:
            st->dest_addr = (uint32_t)addr;
            switch (st->addr_mode) {
            case BTFW_ADDR_MODE_EXTENDED:
                st->dest_addr += ((uint32_t)st->hi_addr) << 16;
                break;
            case BTFW_ADDR_MODE_SEGMENT:
                st->dest_addr += ((uint32_t)st->hi_addr) << 4;
                break;
            case BTFW_ADDR_MODE_LINEAR32:
                st->dest_addr += st->abs_base_addr32;
                break;
            default: break;
            }
            return num_bytes;
        case BTFW_HEX_LINE_END_OF_DATA:
        default:
            break;
        }
    }
    return 0U;
}

/*---------------------------------------------------------------------------*/
/* Bring-up steps                                                            */
/*---------------------------------------------------------------------------*/

static int bt_power_up(void)
{
    int rc = cyw43_gspi_bp_write32(BT_BASE_ADDR + BT2WLAN_PWRUP_OFFSET,
                                   BT2WLAN_PWRUP_WAKE);
    if (rc != TIKU_DRV_OK) {
        TIKU_BT_TX_PRINTF("p6.A: power-up FAIL rc=%d\n", rc);
        return rc;
    }
    tiku_common_delay_ms(2U);
    TIKU_BT_TX_PRINTF("p6.A: power-up ok (BT2WLAN_PWRUP_WAKE)\n");
    return TIKU_DRV_OK;
}

static int bt_upload_firmware(void)
{
    if (cyw43_btfw_size < 4U) {
        TIKU_BT_TX_PRINTF("p6.A: btfw blob too small (%lu B)\n",
                          (unsigned long)cyw43_btfw_size);
        return TIKU_DRV_ERR_INVALID;
    }
    {
        uint8_t       ver_len = cyw43_btfw_data[0];
        uint32_t      skip    = 1U + (uint32_t)ver_len + 1U;
        btfw_cursor_t cur = {
            .p         = cyw43_btfw_data + skip,
            .remaining = cyw43_btfw_size - skip,
        };
        btfw_state_t  st;
        uint32_t      bytes_written = 0UL;

        /* Cache the BTFW header version string for the public
         * tiku_bt_fw_version() getter. The header bytes aren't
         * NUL-terminated in the blob (ver_len in byte 0), so we
         * copy + cap + NUL ourselves. */
        {
            uint8_t copy = ver_len;
            uint8_t i;
            if (copy > (uint8_t)(sizeof tx_state.fw_version - 1U))
                copy = (uint8_t)(sizeof tx_state.fw_version - 1U);
            for (i = 0U; i < copy; ++i)
                tx_state.fw_version[i] = (char)cyw43_btfw_data[1U + i];
            tx_state.fw_version[copy] = '\0';
        }

        st.addr_mode       = BTFW_ADDR_MODE_UNKNOWN;
        st.hi_addr         = 0U;
        st.abs_base_addr32 = 0UL;
        st.dest_addr       = 0UL;

        TIKU_BT_TX_PRINTF("p6.A: uploading BT firmware (%lu B blob, "
                          "skipping %lu B header)\n",
                          (unsigned long)cyw43_btfw_size,
                          (unsigned long)skip);

        while (1) {
            uint8_t  aligned[260];
            uint32_t n = btfw_next_data_record(&cur, &st);
            uint32_t dest;
            uint32_t pad_lo, pad_hi;
            uint32_t aligned_len;
            int      rc;

            if (n == 0U) break;
            dest = st.dest_addr + BT_BASE_ADDR;

            pad_lo = dest & 0x3U;
            aligned_len = 0U;
            if (pad_lo != 0U) {
                uint32_t base = dest & ~0x3UL;
                uint32_t mem  = 0UL;
                rc = cyw43_gspi_bp_read32(base, &mem);
                if (rc != TIKU_DRV_OK) {
                    TIKU_BT_TX_PRINTF("p6.A: rmw read FAIL rc=%d "
                                      "addr=%08lx\n", rc,
                                      (unsigned long)base);
                    return rc;
                }
                aligned[0] = (uint8_t)( mem        & 0xFFU);
                aligned[1] = (uint8_t)((mem >>  8) & 0xFFU);
                aligned[2] = (uint8_t)((mem >> 16) & 0xFFU);
                aligned_len = pad_lo;
                dest = base;
            }
            {
                uint32_t i;
                for (i = 0U; i < n; ++i) aligned[aligned_len + i] = st.data[i];
                aligned_len += n;
            }
            pad_hi = aligned_len & 0x3U;
            if (pad_hi != 0U) {
                uint32_t tail_base = (dest + aligned_len) & ~0x3UL;
                uint32_t mem       = 0UL;
                uint32_t need      = 4U - pad_hi;
                uint32_t i;
                rc = cyw43_gspi_bp_read32(tail_base, &mem);
                if (rc != TIKU_DRV_OK) {
                    TIKU_BT_TX_PRINTF("p6.A: rmw tail FAIL rc=%d "
                                      "addr=%08lx\n", rc,
                                      (unsigned long)tail_base);
                    return rc;
                }
                for (i = pad_hi; i < 4U; ++i) {
                    aligned[aligned_len + (i - pad_hi)] =
                        (uint8_t)((mem >> (i * 8U)) & 0xFFU);
                }
                aligned_len += need;
            }

            rc = cyw43_gspi_bp_write(dest, aligned, aligned_len);
            if (rc != TIKU_DRV_OK) {
                TIKU_BT_TX_PRINTF("p6.A: bp_write FAIL rc=%d dest=%08lx "
                                  "len=%lu\n", rc, (unsigned long)dest,
                                  (unsigned long)aligned_len);
                return rc;
            }
            bytes_written += aligned_len;
            tiku_watchdog_kick();
        }
        TIKU_BT_TX_PRINTF("p6.A: upload done, %lu B written\n",
                          (unsigned long)bytes_written);
    }
    return TIKU_DRV_OK;
}

static int bt_wait_fw_ready(void)
{
    unsigned int polls;
    for (polls = 0U; polls < 300U; ++polls) {
        uint32_t v = 0UL;
        int rc = cyw43_gspi_bp_read32(BT_CTRL_REG_ADDR, &v);
        if (rc == TIKU_DRV_OK && (v & BTSDIO_REG_FW_RDY_BIT) != 0UL) {
            TIKU_BT_TX_PRINTF("p6.A: FW_RDY set after %u polls "
                              "(BT_CTRL=0x%08lx) *** BT firmware "
                              "running ***\n",
                              polls, (unsigned long)v);
            return TIKU_DRV_OK;
        }
        tiku_common_delay_ms(1U);
    }
    TIKU_BT_TX_PRINTF("p6.A: FW_RDY never set (300 polls × 1 ms)\n");
    return TIKU_DRV_ERR_TIMEOUT;
}

static int bt_wait_bt_awake(void)
{
    unsigned int polls;
    for (polls = 0U; polls < 300U; ++polls) {
        uint32_t v = 0UL;
        int rc = cyw43_gspi_bp_read32(BT_CTRL_REG_ADDR, &v);
        if (rc == TIKU_DRV_OK && (v & BTSDIO_REG_BT_AWAKE_BIT) != 0UL) {
            TIKU_BT_TX_PRINTF("p6.B: BT_AWAKE set after %u polls\n", polls);
            return TIKU_DRV_OK;
        }
        tiku_common_delay_ms(1U);
    }
    TIKU_BT_TX_PRINTF("p6.B: BT_AWAKE never set (300 polls × 1 ms)\n");
    return TIKU_DRV_ERR_TIMEOUT;
}

static int bt_init_buffers(void)
{
    uint32_t base = 0UL;
    int      rc = cyw43_gspi_bp_read32(WLAN_RAM_BASE_REG_ADDR, &base);
    if (rc != TIKU_DRV_OK || base == 0UL) {
        TIKU_BT_TX_PRINTF("p6.B: WLAN_RAM_BASE read FAIL rc=%d "
                          "base=%08lx\n", rc, (unsigned long)base);
        return (rc == TIKU_DRV_OK) ? TIKU_DRV_ERR_NOT_PRESENT : rc;
    }
    tx_state.ring_base     = base;
    tx_state.h2b_write_ptr = 0UL;
    tx_state.b2h_read_ptr  = 0UL;

    TIKU_BT_TX_PRINTF("p6.B: ring base = 0x%08lx; zeroing pointers\n",
                      (unsigned long)base);

    (void)cyw43_gspi_bp_write32(base + BTSDIO_OFFSET_H2BT_IN,  0UL);
    (void)cyw43_gspi_bp_write32(base + BTSDIO_OFFSET_H2BT_OUT, 0UL);
    (void)cyw43_gspi_bp_write32(base + BTSDIO_OFFSET_BT2H_IN,  0UL);
    (void)cyw43_gspi_bp_write32(base + BTSDIO_OFFSET_BT2H_OUT, 0UL);
    return TIKU_DRV_OK;
}

static int bt_handshake(void)
{
    uint32_t v   = 0UL;
    int      rc;

    rc = cyw43_gspi_bp_read32(HOST_CTRL_REG_ADDR, &v);
    if (rc != TIKU_DRV_OK) return rc;
    v |= BTSDIO_REG_SW_RDY_BIT;
    rc = cyw43_gspi_bp_write32(HOST_CTRL_REG_ADDR, v);
    if (rc != TIKU_DRV_OK) return rc;

    rc = cyw43_gspi_bp_read32(HOST_CTRL_REG_ADDR, &v);
    if (rc != TIKU_DRV_OK) return rc;
    v ^= BTSDIO_REG_DATA_VALID_BIT;
    rc = cyw43_gspi_bp_write32(HOST_CTRL_REG_ADDR, v);
    if (rc != TIKU_DRV_OK) return rc;

    TIKU_BT_TX_PRINTF("p6.B: SW_RDY set, DATA_VALID toggled "
                      "*** BT bring-up complete ***\n");
    return TIKU_DRV_OK;
}

/*---------------------------------------------------------------------------*/
/* Per-packet helpers (used by send/recv)                                    */
/*---------------------------------------------------------------------------*/

/** Round @p n up to the next multiple of 4. */
static uint32_t btsdio_round4(uint32_t n) { return (n + 3U) & ~3U; }

/** Tell the chip to wake the BT subsystem and wait until awake. */
static int bt_bus_request(void)
{
    uint32_t v = 0UL;
    int      rc = cyw43_gspi_bp_read32(HOST_CTRL_REG_ADDR, &v);
    unsigned int polls;

    if (rc != TIKU_DRV_OK) return rc;
    v |= BTSDIO_REG_WAKE_BT_BIT;
    rc = cyw43_gspi_bp_write32(HOST_CTRL_REG_ADDR, v);
    if (rc != TIKU_DRV_OK) return rc;

    for (polls = 0U; polls < 100U; ++polls) {
        uint32_t ctrl = 0UL;
        rc = cyw43_gspi_bp_read32(BT_CTRL_REG_ADDR, &ctrl);
        if (rc == TIKU_DRV_OK && (ctrl & BTSDIO_REG_BT_AWAKE_BIT) != 0UL) {
            return TIKU_DRV_OK;
        }
        tiku_common_delay_ms(1U);
    }
    return TIKU_DRV_ERR_TIMEOUT;
}

/** Toggle the DATA_VALID bit in HOST_CTRL_REG to nudge the chip. */
static int bt_toggle_data_valid(void)
{
    uint32_t v = 0UL;
    int      rc = cyw43_gspi_bp_read32(HOST_CTRL_REG_ADDR, &v);
    if (rc != TIKU_DRV_OK) return rc;
    v ^= BTSDIO_REG_DATA_VALID_BIT;
    return cyw43_gspi_bp_write32(HOST_CTRL_REG_ADDR, v);
}

/** Read N word-aligned bytes from a backplane address. */
static int bt_bp_read_words(uint32_t bp_addr, uint8_t *out, uint32_t len)
{
    uint32_t i;
    for (i = 0U; i < len; i += 4U) {
        uint32_t w = 0UL;
        int rc = cyw43_gspi_bp_read32(bp_addr + i, &w);
        if (rc != TIKU_DRV_OK) return rc;
        out[i + 0] = (uint8_t)( w        & 0xFFU);
        out[i + 1] = (uint8_t)((w >>  8) & 0xFFU);
        out[i + 2] = (uint8_t)((w >> 16) & 0xFFU);
        out[i + 3] = (uint8_t)((w >> 24) & 0xFFU);
    }
    return TIKU_DRV_OK;
}

/*---------------------------------------------------------------------------*/
/* BTSDIO ring send / recv (vtable callbacks)                                */
/*---------------------------------------------------------------------------*/

static int cyw43_bt_is_ready_cb(void)
{
    return tx_state.fw_ready ? 1 : 0;
}

static int cyw43_bt_send_cb(const uint8_t *packet, uint16_t len)
{
    if (tx_state.fw_ready == 0U) return TIKU_DRV_ERR_NOT_PRESENT;
    if (packet == (const uint8_t *)0 || len < 2U) return TIKU_DRV_ERR_INVALID;

    {
        int      rc;
        uint8_t  type        = packet[0];
        uint32_t payload_len = (uint32_t)len - 1U;
        uint32_t rounded     = btsdio_round4(payload_len);
        uint32_t total       = 4U + rounded;
        uint32_t read_ptr    = 0UL;
        uint32_t avail;
        uint8_t  hdr[4] __attribute__((aligned(4)));
        uint32_t addr;

        rc = bt_bus_request();
        if (rc != TIKU_DRV_OK) return rc;

        rc = cyw43_gspi_bp_read32(tx_state.ring_base +
                                  BTSDIO_OFFSET_H2BT_OUT, &read_ptr);
        if (rc != TIKU_DRV_OK) return rc;

        avail = (read_ptr - (tx_state.h2b_write_ptr + 4U))
                & (BTSDIO_FWBUF_SIZE - 1U);
        if (avail < total) {
            TIKU_BT_TX_PRINTF("tx ring full (avail=%lu need=%lu)\n",
                              (unsigned long)avail, (unsigned long)total);
            return TIKU_DRV_ERR_TIMEOUT;
        }

        hdr[0] = (uint8_t)( payload_len        & 0xFFU);
        hdr[1] = (uint8_t)((payload_len >>  8) & 0xFFU);
        hdr[2] = (uint8_t)((payload_len >> 16) & 0xFFU);
        hdr[3] = type;

        addr = tx_state.ring_base + BTSDIO_OFFSET_HOST_WRITE
             + tx_state.h2b_write_ptr;
        rc = cyw43_gspi_bp_write(addr, hdr, 4U);
        if (rc != TIKU_DRV_OK) return rc;
        tx_state.h2b_write_ptr =
            (tx_state.h2b_write_ptr + 4U) & (BTSDIO_FWBUF_SIZE - 1U);

        {
            uint32_t i;
            if (rounded > BT_TX_SCRATCH_SIZE) return TIKU_DRV_ERR_INVALID;
            for (i = 0U; i < payload_len; ++i)
                tx_scratch[i] = packet[1U + i];
            for (; i < rounded; ++i) tx_scratch[i] = 0U;

            if (tx_state.h2b_write_ptr + rounded > BTSDIO_FWBUF_SIZE) {
                uint32_t first = BTSDIO_FWBUF_SIZE - tx_state.h2b_write_ptr;
                addr = tx_state.ring_base + BTSDIO_OFFSET_HOST_WRITE
                     + tx_state.h2b_write_ptr;
                rc = cyw43_gspi_bp_write(addr, tx_scratch, first);
                if (rc != TIKU_DRV_OK) return rc;
                addr = tx_state.ring_base + BTSDIO_OFFSET_HOST_WRITE;
                rc = cyw43_gspi_bp_write(addr, tx_scratch + first,
                                         rounded - first);
                if (rc != TIKU_DRV_OK) return rc;
            } else {
                addr = tx_state.ring_base + BTSDIO_OFFSET_HOST_WRITE
                     + tx_state.h2b_write_ptr;
                rc = cyw43_gspi_bp_write(addr, tx_scratch, rounded);
                if (rc != TIKU_DRV_OK) return rc;
            }
            tx_state.h2b_write_ptr =
                (tx_state.h2b_write_ptr + rounded) & (BTSDIO_FWBUF_SIZE - 1U);
        }

        rc = cyw43_gspi_bp_write32(tx_state.ring_base +
                                   BTSDIO_OFFSET_H2BT_IN,
                                   tx_state.h2b_write_ptr);
        if (rc != TIKU_DRV_OK) return rc;

        return bt_toggle_data_valid();
    }
}

static int cyw43_bt_recv_cb(uint8_t *out, uint16_t out_max)
{
    if (tx_state.fw_ready == 0U) return TIKU_DRV_ERR_NOT_PRESENT;
    if (out == (uint8_t *)0 || out_max < 2U) return TIKU_DRV_ERR_INVALID;

    {
        uint32_t write_ptr = 0UL;
        uint32_t avail;
        uint8_t  hdr[4] __attribute__((aligned(4)));
        uint32_t addr;
        uint32_t payload_len;
        uint32_t rounded;
        uint8_t  type;
        int      rc;

        rc = cyw43_gspi_bp_read32(tx_state.ring_base +
                                  BTSDIO_OFFSET_BT2H_IN, &write_ptr);
        if (rc != TIKU_DRV_OK) return rc;

        avail = (write_ptr - tx_state.b2h_read_ptr)
                & (BTSDIO_FWBUF_SIZE - 1U);
        if (avail == 0U) return 0;

        addr = tx_state.ring_base + BTSDIO_OFFSET_HOST_READ
             + tx_state.b2h_read_ptr;
        rc = bt_bp_read_words(addr, hdr, 4U);
        if (rc != TIKU_DRV_OK) return rc;

        payload_len = (uint32_t)hdr[0]
                    | ((uint32_t)hdr[1] <<  8)
                    | ((uint32_t)hdr[2] << 16);
        type        = hdr[3];
        rounded     = btsdio_round4(payload_len);

        if (avail < 4U + rounded) return 0; /* partial frame, retry later */
        if (1U + payload_len > (uint32_t)out_max) return TIKU_DRV_ERR_INVALID;

        tx_state.b2h_read_ptr =
            (tx_state.b2h_read_ptr + 4U) & (BTSDIO_FWBUF_SIZE - 1U);

        out[0] = type;
        {
            uint32_t i;
            if (rounded > BT_TX_SCRATCH_SIZE) return TIKU_DRV_ERR_INVALID;
            if (tx_state.b2h_read_ptr + rounded > BTSDIO_FWBUF_SIZE) {
                uint32_t first = BTSDIO_FWBUF_SIZE - tx_state.b2h_read_ptr;
                addr = tx_state.ring_base + BTSDIO_OFFSET_HOST_READ
                     + tx_state.b2h_read_ptr;
                rc = bt_bp_read_words(addr, tx_scratch, first);
                if (rc != TIKU_DRV_OK) return rc;
                addr = tx_state.ring_base + BTSDIO_OFFSET_HOST_READ;
                rc = bt_bp_read_words(addr, tx_scratch + first,
                                      rounded - first);
                if (rc != TIKU_DRV_OK) return rc;
            } else {
                addr = tx_state.ring_base + BTSDIO_OFFSET_HOST_READ
                     + tx_state.b2h_read_ptr;
                rc = bt_bp_read_words(addr, tx_scratch, rounded);
                if (rc != TIKU_DRV_OK) return rc;
            }
            for (i = 0U; i < payload_len; ++i)
                out[1U + i] = tx_scratch[i];
        }
        tx_state.b2h_read_ptr =
            (tx_state.b2h_read_ptr + rounded) & (BTSDIO_FWBUF_SIZE - 1U);

        rc = cyw43_gspi_bp_write32(tx_state.ring_base +
                                   BTSDIO_OFFSET_BT2H_OUT,
                                   tx_state.b2h_read_ptr);
        if (rc != TIKU_DRV_OK) return rc;

        (void)bt_toggle_data_valid();
        return (int)(1U + payload_len);
    }
}

/*---------------------------------------------------------------------------*/
/* Transport vtable + entry point                                            */
/*---------------------------------------------------------------------------*/

static const tiku_bt_transport_t cyw43_bt_transport = {
    .send     = cyw43_bt_send_cb,
    .recv     = cyw43_bt_recv_cb,
    .is_ready = cyw43_bt_is_ready_cb,
};

/** Public BTFW version string getter — used by the generic stack
 *  via the weak override in tiku_bt.c, kept simple here. */
const char *cyw43_bt_fw_version(void)
{
    return tx_state.fw_version;
}

int cyw43_bt_init(void)
{
    int rc;

    tx_state.fw_ready      = 0U;
    tx_state.ring_base     = 0UL;
    tx_state.h2b_write_ptr = 0UL;
    tx_state.b2h_read_ptr  = 0UL;

    /* Arena-backed staging scratch (id=0xB2). Idempotent: a second
     * call to cyw43_bt_init reuses the existing buffer rather than
     * re-creating it (the arena is a bump-allocator; recreate would
     * lose the first allocation). The generic BT stack's arena
     * (id=0xB1) lives in tiku_bt.c. */
    if (tx_scratch == (uint8_t *)0) {
        tiku_mem_err_t err = tiku_arena_create(&bt_tx_arena, bt_tx_arena_buf,
                              (tiku_mem_arch_size_t)sizeof bt_tx_arena_buf,
                              /* id */ 0xB2U);
        if (err != TIKU_MEM_OK) {
            TIKU_BT_TX_PRINTF("tiku_arena_create err=%d\n", (int)err);
            return TIKU_DRV_ERR_NOT_PRESENT;
        }
        tx_scratch = (uint8_t *)tiku_arena_alloc(&bt_tx_arena,
                              BT_TX_SCRATCH_SIZE);
        if (tx_scratch == (uint8_t *)0) {
            TIKU_BT_TX_PRINTF("arena alloc FAIL (need %u B)\n",
                              (unsigned)BT_TX_SCRATCH_SIZE);
            return TIKU_DRV_ERR_INVALID;
        }
        {
            tiku_mem_stats_t s;
            if (tiku_arena_stats(&bt_tx_arena, &s) == TIKU_MEM_OK) {
                TIKU_BT_TX_PRINTF("arena id=0xB2 used=%lu/%lu B\n",
                                  (unsigned long)s.used_bytes,
                                  (unsigned long)s.total_bytes);
            }
        }
    }

    rc = bt_power_up();
    if (rc != TIKU_DRV_OK) return rc;

    rc = bt_upload_firmware();
    if (rc != TIKU_DRV_OK) return rc;

    rc = bt_wait_fw_ready();
    if (rc != TIKU_DRV_OK) return rc;
    tx_state.fw_ready = 1U;

    rc = bt_init_buffers();
    if (rc != TIKU_DRV_OK) return rc;

    rc = bt_wait_bt_awake();
    if (rc != TIKU_DRV_OK) return rc;

    rc = bt_handshake();
    if (rc != TIKU_DRV_OK) return rc;

    /* Transport is now able to send / recv HCI packets. Register
     * with the generic stack and hand off to the protocol init. */
    rc = tiku_bt_register_transport(&cyw43_bt_transport);
    if (rc != 0) {
        TIKU_BT_TX_PRINTF("transport register FAIL rc=%d\n", rc);
        return rc;
    }
    return tiku_bt_init();
}
