/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * gspi.h - CYW43439 gSPI transport (PIO-driven)
 *
 * The CYW43439 talks to the host over a custom 4-wire SPI variant
 * ("gSPI"):
 *
 *   CLK  - output, host-driven clock
 *   CS   - output, active low
 *   DATA - bidirectional, MSB-first
 *   IRQ  - input from chip (shares the DATA pin between
 *          transactions; the chip pulls it high when it wants
 *          host attention)
 *
 * The bidirectional DATA line is what forces PIO — RP2350's SPI
 * peripheral assumes separate MOSI/MISO pins. PIO1 runs a small
 * state-machine program that drives CLK + DATA in the requested
 * direction, with CS asserted around the transaction.
 *
 * All transactions are 32-bit aligned once BUS_CTRL has been configured.
 * Before that point the chip answers in its reset-default 16-bit word
 * mode, so the phase-1 probe uses a 16-bit halfword swap for the initial
 * TEST_RO / TEST_RW / BUS_CTRL transactions.
 *
 * Word ordering on the configured wire:
 *
 *   1. 32-bit command word
 *      - bit 31:    R/W   (1 = write, 0 = read)
 *      - bit 30:    A      (1 = increment address)
 *      - bits 29:28: function (0 = bus, 1 = backplane, 2 = WLAN)
 *      - bits 27:11: address (within function)
 *      - bits 10:0:  byte count
 *   2. N data words (read or write). Backplane reads add their
 *      configured response-delay padding in the higher-level block path.
 *
 * See the CYW43439 datasheet's "gSPI Protocol" section for the
 * full field layout; this header captures the bits the driver
 * actively uses, and the rest of the encoding lives in gspi.c.
 *
 * Phase 1 deliverable: gspi_init() brings the chip out of reset,
 * the four read/write functions clock data, and the F0 test
 * register (function=0, addr=0x14) reads back 0xFEEDBEAD.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_CYW43_GSPI_H_
#define TIKU_DRV_WIFI_CYW43_GSPI_H_

#include <stdint.h>
#include <stddef.h>
#include "kernel/drivers/tiku_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* gSPI function codes                                                       */
/*---------------------------------------------------------------------------*/

#define GSPI_FUNCTION_BUS         0U  /* bus-control regs (chip ID, status) */
#define GSPI_FUNCTION_BACKPLANE   1U  /* AXI backplane (firmware upload)    */
#define GSPI_FUNCTION_WLAN        2U  /* WLAN command/data path             */

/*---------------------------------------------------------------------------*/
/* Bus-function registers (function = 0)                                     */
/*---------------------------------------------------------------------------*/
/* Per the CYW43439 gSPI register map, addresses below are within function 0.
 * Only the ones the phase-1 bus probe needs are defined here;
 * the rest move in alongside their consumers in phases 2+. */

#define GSPI_REG_SPI_BUS_CTRL     0x0000U  /* endianness, wrap, irq pol */
#define GSPI_REG_SPI_RESP_DELAY_0 0x001CU
#define GSPI_REG_SPI_RESP_DELAY_1 0x001DU
#define GSPI_REG_SPI_RESP_DELAY_2 0x001EU
#define GSPI_REG_SPI_RESP_DELAY_3 0x001FU
#define GSPI_REG_SPI_STATUS_EN    0x0002U
#define GSPI_REG_SPI_RESET_BP     0x0003U
#define GSPI_REG_SPI_INT_REG      0x0004U  /* IRQ status (R/W1C) */
#define GSPI_REG_SPI_INT_EN_REG   0x0006U  /* per-source IRQ mask */
#define GSPI_REG_SPI_STATUS       0x0008U
#define GSPI_REG_SPI_FUNC1_INFO   0x000CU
#define GSPI_REG_SPI_TEST_RO      0x0014U  /* SPI_TEST_RO = 0xFEEDBEAD */
#define GSPI_REG_SPI_TEST_RW      0x0018U
#define GSPI_REG_CHIP_ID          GSPI_REG_SPI_TEST_RO

/* SPI_BUS_CTRL byte 0 bits. */
#define GSPI_BUS_CTRL_WORD_LENGTH_32  (1U << 0)
#define GSPI_BUS_CTRL_ENDIAN_BIG      (1U << 1)
#define GSPI_BUS_CTRL_HIGH_SPEED      (1U << 4)
#define GSPI_BUS_CTRL_INT_POL_HIGH    (1U << 5)
#define GSPI_BUS_CTRL_WAKE_UP         (1U << 7)

/* SPI_BUS_CTRL is a 32-bit F0 register. Byte 1 is the response delay,
 * byte 2 is SPI_STATUS_ENABLE. Keeping the byte positions explicit avoids
 * confusing them with the standalone F0 register aliases at 0x1c..0x1f. */
#define GSPI_BUS_CTRL_RESP_DELAY_SHIFT     8U
#define GSPI_BUS_CTRL_STATUS_ENABLE_SHIFT 16U

#define GSPI_STATUS_ENABLE            0x01U
#define GSPI_INTR_WITH_STATUS         0x02U
/* Response delay = 0 bytes for phase 1. Codex's original 0x04 (= 32
 * wire clocks of delay before chip drives response) put chip's data
 * outside our sample window. Empirically, chip without programmed
 * delay drives response with ~0-1 wire cycles of latency, matching
 * what cyw43-driver assumes. */
#define GSPI_RESP_DELAY_PHASE1        0x00U

/* Wake/configure write per embassy-rs cyw43 reference (the working
 * Apache-2.0 driver). Critical: NO ENDIAN_BIG — chip's default
 * endianness is what we want. Setting ENDIAN_BIG put the chip in
 * a state where every register read echoed the wake byte
 * (0xB3B3B3B3 saw earlier). The right byte 0 value is 0xB1 (= no
 * 0x02 bit). */
#define GSPI_BUS_CTRL_PHASE2_WAKE \
    ((GSPI_BUS_CTRL_WORD_LENGTH_32 | \
      GSPI_BUS_CTRL_HIGH_SPEED     | \
      GSPI_BUS_CTRL_INT_POL_HIGH   | \
      GSPI_BUS_CTRL_WAKE_UP) | \
     (0x04UL << GSPI_BUS_CTRL_RESP_DELAY_SHIFT) | \
     ((GSPI_STATUS_ENABLE | GSPI_INTR_WITH_STATUS) << \
      GSPI_BUS_CTRL_STATUS_ENABLE_SHIFT))

/* Backwards compat for callers using the old name. */
#define GSPI_BUS_CTRL_PHASE1_WAKE GSPI_BUS_CTRL_PHASE2_WAKE

/* Backplane addressing: bus_addr = (addr & MASK) | FLAG32 (when 4-byte).
 * Window is 32 KB — high 17 bits of backplane address come from
 * SBADDR registers (set via 0x1000A/B/C). */
#define GSPI_BACKPLANE_ADDRESS_MASK    0x00007FFFUL
#define GSPI_BACKPLANE_ADDRESS_32B_FLAG 0x00008000UL

/* SPI_RESP_DELAY_F1 (= F0 byte 0x1D) holds the host-side padding
 * count between an F1 read cmd and the first response byte. WHD/
 * embassy use 4 for backplane reads. */
#define GSPI_REG_SPI_RESP_DELAY_F1     0x001DU
#define GSPI_BACKPLANE_READ_PADD_BYTES 0x04U

/* SBADDR low/mid/high in the F1 backplane register space. */
#define GSPI_REG_SBADDR_LOW            0x1000AUL
#define GSPI_REG_SBADDR_MID            0x1000BUL
#define GSPI_REG_SBADDR_HIGH           0x1000CUL

/* Backplane core base addresses (CYW43439). The "wrapper" form is the
 * AI bus register block for the core (where reset/IO control regs
 * live); the non-wrapper form is the core's own register window. */
#define GSPI_BACKPLANE_CHIPCOMMON_BASE 0x18000000UL
#define GSPI_BACKPLANE_SDIOD_BASE      0x18002000UL
#define GSPI_BACKPLANE_ARM_CORE_BASE   0x18103000UL  /* = 0x18003000 + wrap 0x100000 */
#define GSPI_BACKPLANE_SOCSRAM_BASE    0x18004000UL
#define GSPI_BACKPLANE_SOCSRAM_WRAP    0x18104000UL  /* = 0x18004000 + wrap 0x100000 */

/* AI (AMBA Interconnect) wrapper register offsets within each core. */
#define GSPI_AI_IOCTRL_OFFSET          0x408UL
#define GSPI_AI_RESETCTRL_OFFSET       0x800UL
#define GSPI_AI_RESETSTATUS_OFFSET     0x804UL

/* AI register bits. */
#define GSPI_AI_IOCTRL_BIT_CLOCK_EN    0x01U
#define GSPI_AI_IOCTRL_BIT_FGC         0x02U  /* Force Gated Clock */
#define GSPI_AI_IOCTRL_BIT_CPUHALT     0x20U
#define GSPI_AI_RESETCTRL_BIT_RESET    0x01U

/* CYW43439 chip RAM (where firmware gets loaded). */
#define GSPI_CHIP_RAM_BASE             0x00000000UL
#define GSPI_CHIP_RAM_SIZE             (512UL * 1024UL)

/* CHIP_CLOCK_CSR sits in the F1 (FUNC_BACKPLANE) address space at
 * raw address 0x1000E — it is NOT a backplane window offset, so it
 * is reached via cyw43_gspi_read8(GSPI_FUNCTION_BACKPLANE, ...) and
 * SBADDR is irrelevant for it. Bits:
 *   ALP_AVAIL_REQ (0x08): host asks chip to bring up ALP clock
 *   ALP_AVAIL     (0x40): chip ack — ALP clock running
 *   HT_AVAIL_REQ  (0x10): host asks chip to bring up HT clock
 *   HT_AVAIL      (0x80): chip ack — HT clock running, firmware booted
 */
#define GSPI_REG_CHIP_CLOCK_CSR        0x1000EUL
#define GSPI_CHIP_CLOCK_ALP_AVAIL_REQ  0x08U
#define GSPI_CHIP_CLOCK_ALP_AVAIL      0x40U
#define GSPI_CHIP_CLOCK_HT_AVAIL_REQ   0x10U
#define GSPI_CHIP_CLOCK_HT_AVAIL       0x80U

/* SPI_STATUS register (F0/0x0008) bits — useful for the post-boot
 * health probe (and the phase-3 WHD packet path). */
#define GSPI_STATUS_DATA_NOT_AVAIL     0x00000001UL
#define GSPI_STATUS_UNDERFLOW          0x00000002UL
#define GSPI_STATUS_OVERFLOW           0x00000004UL
#define GSPI_STATUS_F2_INTR            0x00000008UL
#define GSPI_STATUS_F2_RX_READY        0x00000020UL
#define GSPI_STATUS_HOST_CMD_DATA_ERR  0x00000080UL
#define GSPI_STATUS_F2_PKT_AVAILABLE   0x00000100UL
#define GSPI_STATUS_F2_PKT_LEN_MASK    0x000FFE00UL
#define GSPI_STATUS_F2_PKT_LEN_SHIFT   9U

/* SPI_INT_EN_REG bits (16-bit register at F0/0x0006 = bytes 2..3 of
 * the 32-bit word at F0/0x0004). Bit 5 (0x0020) = enable F2 packet
 * available IRQ. Phase 3.A enables this so the chip starts gating
 * F2_RX_READY in SPI_STATUS on watermark+IRQ-mask state. */
#define GSPI_IRQ_DATA_UNAVAILABLE      0x0001U
#define GSPI_IRQ_F2_F3_UNDERFLOW       0x0002U
#define GSPI_IRQ_F2_F3_OVERFLOW        0x0004U
#define GSPI_IRQ_COMMAND_ERROR         0x0008U
#define GSPI_IRQ_DATA_ERROR            0x0010U
#define GSPI_IRQ_F2_PACKET_AVAILABLE   0x0020U
#define GSPI_IRQ_F1_OVERFLOW           0x0080U
#define GSPI_IRQ_F1_INTR               0x2000U
#define GSPI_IRQ_F2_INTR               0x4000U

/* REG_BACKPLANE_FUNCTION2_WATERMARK is in the F1 (FUNC_BACKPLANE)
 * address space at raw 0x10008, like CHIP_CLOCK_CSR — reached via
 * cyw43_gspi_write8(F1, 0x10008, val), no SBADDR involvement.
 *
 * SPI_F2_WATERMARK = 0x20 (32 bytes) per embassy-rs cyw43 consts.rs;
 * the comment there says "Lower F2 Watermark to avoid DMA Hang in F2
 * when SD Clock is stopped" — copy the value, trust the warning. */
#define GSPI_REG_F2_WATERMARK          0x10008UL
#define GSPI_F2_WATERMARK_BYTES        0x20U

/*---------------------------------------------------------------------------*/
/* Expected IDs / test patterns                                              */
/*---------------------------------------------------------------------------*/
/* The real chip ID lives behind the backplane and is not the phase-1
 * acceptance check. SPI_TEST_RO is the bus-alive register. */
/* CYW43439 chipcommon ChipID in low 16 bits = 43439 decimal = 0xA9AF.
 * (Older WHD/BCM43xx docs reference 0xA9A6 but that's a different
 * silicon revision; this chip on Pi Pico 2 W reports 0xA9AF, as
 * embassy-rs cyw43 also expects.) */
#define GSPI_CHIP_ID_CYW43439     0xA9AFU
#define GSPI_CHIP_ID_MASK         0x0000FFFFUL
#define GSPI_TEST_RO_PATTERN       0xFEEDBEADUL
#define GSPI_TEST_RO_PATTERN_BSWAP 0xBEADFEEDUL
#define GSPI_TEST_RW_PATTERN       0x12345678UL

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bring up the gSPI transport: drive WL_REG_ON high,
 *        configure the four CYW43 pins, load the PIO program,
 *        wait for the chip's startup ready window.
 *
 * Idempotent. Returns TIKU_DRV_OK on success; one of the
 * TIKU_DRV_ERR_* codes on failure (typically ERR_TIMEOUT if the
 * chip never reports ready).
 */
int cyw43_gspi_init(void);

/**
 * @brief Power-gate the chip via WL_REG_ON low. Subsequent gSPI
 *        calls fail until cyw43_gspi_init() runs again.
 */
void cyw43_gspi_deinit(void);

/**
 * @brief Read one 32-bit register over gSPI.
 *
 * @param function  GSPI_FUNCTION_BUS / _BACKPLANE / _WLAN
 * @param address   17-bit address within the function space
 * @param out_value where to store the read value
 * @return TIKU_DRV_OK or TIKU_DRV_ERR_*
 */
int cyw43_gspi_read32(uint8_t function, uint32_t address,
                      uint32_t *out_value);

/**
 * @brief Write one 32-bit register over gSPI.
 */
int cyw43_gspi_write32(uint8_t function, uint32_t address,
                       uint32_t value);

/**
 * @brief Read a block of bytes. `byte_count` is rounded up to a
 *        4-byte boundary on the wire; `buf` must be at least that
 *        many bytes. Used by the firmware-upload path.
 */
int cyw43_gspi_read_block(uint8_t function, uint32_t address,
                          uint8_t *buf, size_t byte_count);

/**
 * @brief Write a block of bytes; same alignment rules as read.
 */
int cyw43_gspi_write_block(uint8_t function, uint32_t address,
                           const uint8_t *buf, size_t byte_count);

/**
 * @brief Read 1 byte from a chip-side register via the appropriate
 *        function. For F1 (backplane), the cmd uses fixed-addr mode
 *        and reads 2 wire words (response-delay padding + data).
 */
int cyw43_gspi_read8(uint8_t function, uint32_t address,
                     uint8_t *out_value);

/**
 * @brief Write 1 byte to a chip-side register.
 */
int cyw43_gspi_write8(uint8_t function, uint32_t address, uint8_t value);

/**
 * @brief Backplane (F1) read/write through the SBADDR window —
 *        automatically configures SBADDR_LOW/MID/HIGH to point at
 *        the high bits of the target address, then issues the F1
 *        access at the windowed offset.
 */
int cyw43_gspi_bp_read8(uint32_t bp_addr, uint8_t *out_value);
int cyw43_gspi_bp_write8(uint32_t bp_addr, uint8_t value);
int cyw43_gspi_bp_read32(uint32_t bp_addr, uint32_t *out_value);
int cyw43_gspi_bp_write32(uint32_t bp_addr, uint32_t value);

/**
 * @brief Backplane block write — used by firmware upload.
 *        `data` and `byte_count` should be 4-byte aligned for now.
 */
int cyw43_gspi_bp_write(uint32_t bp_addr, const uint8_t *data,
                        size_t byte_count);

/**
 * @brief Phase-1 acceptance probe. Reads SPI_TEST_RO via
 *        cyw43_gspi_read32(BUS, 0x14) and verifies the predefined
 *        32-bit gSPI test pattern.
 *
 * Called from cyw43_init() once gspi_init succeeds. Pass/fail
 * decides whether to advance to firmware upload.
 */
int cyw43_gspi_probe_chip_id(void);

/**
 * @brief Bounded poll on SPI_STATUS until `(status & mask) ==
 *        expected` or the retry budget runs out.
 *
 * Used by phase 2+ paths (firmware upload, F2-ready handshake)
 * that need to wait for the chip to flip a specific status bit.
 * Returns TIKU_DRV_OK on match, TIKU_DRV_ERR_TIMEOUT on budget
 * exhausted, or whatever read32 returned if the bus itself
 * faulted mid-poll.
 */
int cyw43_gspi_wait_status(uint32_t mask, uint32_t expected);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_DRV_WIFI_CYW43_GSPI_H_ */
