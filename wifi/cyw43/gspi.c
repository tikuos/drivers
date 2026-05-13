/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * gspi.c - CYW43439 gSPI transport (phase 1)
 *
 * Pin and PIO layout:
 *
 *   GP23 (WL_REG_ON)  : SIO output — power gate
 *   GP24 (WL_DATA)    : PIO1 — bidirectional data line
 *   GP25 (WL_CS)      : SIO output — active-low chip select
 *                       (driven directly by CPU around each
 *                       transaction; not folded into the PIO
 *                       side-set so the firmware-upload window
 *                       trick stays available later)
 *   GP29 (WL_CLOCK)   : PIO1 — host-driven SPI clock via
 *                       side-set bit
 *
 *   PIO block         : PIO1 SM0 (PIO0 SM0 is the bitbang engine)
 *   PIO completion    : SM-raised PIO IRQ flag 0, host-polled
 *                       (no NVIC wiring yet; phase 2+ can move
 *                       to NVIC IRQ 17 if the upload path needs
 *                       it)
 *
 * Items landed in this phase:
 *   1. Pin mux for WL_DATA / WL_CLOCK to PIO1, WL_CS to SIO.
 *   2. PIO1 reset release, SM0 configuration (CLKDIV, SHIFTCTRL,
 *      PINCTRL), program load.
 *   3. 32-bit gSPI command-word builder per CYW43439 datasheet.
 *   4. Status-register poll helper (used by phase 2+).
 *
 * Item 5 — the PIO program instructions themselves — is the
 * bench-iteration step the comment block in cyw43_gspi_program[]
 * describes. read32/write32 are wired through gspi_xfer(), and
 * the bus probe in cyw43_gspi_probe_chip_id() reports the
 * SPI_TEST_RO pattern once the chip is decoding reads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gspi.h"
#include "firmware.h"
#include "tiku.h"
#include <arch/arm-rp2350/tiku_rp2350_regs.h>

#ifndef CYW43_PRINTF
#define CYW43_PRINTF(...) TIKU_PRINTF("[cyw43] " __VA_ARGS__)
#endif

/*---------------------------------------------------------------------------*/
/* PIN + PIO ASSIGNMENTS                                                     */
/*---------------------------------------------------------------------------*/

#define WL_REG_ON_PIN   TIKU_BOARD_CYW43_WL_REG_ON_PIN
#define WL_DATA_PIN     TIKU_BOARD_CYW43_WL_DATA_PIN
#define WL_CS_PIN       TIKU_BOARD_CYW43_WL_CS_PIN
#define WL_CLOCK_PIN    TIKU_BOARD_CYW43_WL_CLOCK_PIN

#define CYW43_PIO_BASE  RP2350_PIO1_BASE
#define CYW43_SM        0U

/*
 * Spin budgets:
 *   _SPIN_LIMIT is for one transaction (program-driven). At a
 *   ~9 MHz SM clock and 32 bits each direction, the wire takes
 *   ~7 µs; allow ~1 ms of CPU spin (10000 iterations @ ~10
 *   cycles each / 150 MHz) before declaring the bus stalled.
 *
 *   _STATUS_LIMIT is for the bring-up status poll, which can
 *   take longer (chip-internal logic settling, F2 ready
 *   handshake). 100 retries of ~100 µs each = 10 ms total.
 */
#define CYW43_GSPI_SPIN_LIMIT    10000UL
#define CYW43_GSPI_STATUS_LIMIT  100U

/*---------------------------------------------------------------------------*/
/* DRIVER STATE                                                              */
/*---------------------------------------------------------------------------*/

static struct {
    uint8_t  powered;     /* WL_REG_ON high */
    uint8_t  bus_up;      /* pins muxed + SM configured */
    uint8_t  ready;       /* phase-1 bus probe succeeded */
    uint8_t  configured_32; /* BUS_CTRL switched out of reset defaults */
} gspi;

/*---------------------------------------------------------------------------*/
/* PIO PROGRAM                                                              */
/*---------------------------------------------------------------------------*/
/*
 * gSPI variant matching the Pico SDK's default spi_gap01_sample0
 * timing: after command TX, turn DATA around, raise CLK once, then
 * sample on the following low half-cycle. The SM idles at the
 * blocking pull with DATA as input so the chip can own the shared
 * DATA/IRQ line between transactions.
 *
 * Host pushes:
 *   word 0: x_count = tx_bits - 1
 *   word 1: y_count = rx_bits - 1, or 0 for TX-only writes
 *   word 2: command word
 *   word 3: optional write payload
 *
 * OUT/IN shift MSB-first. The probe handles the chip's reset-default
 * 16-bit word mode by rotating command and payload halfwords before
 * they reach this program.
 */
static const uint16_t cyw43_gspi_program[] = {
    0xE080U,    /* [0]  set pindirs, 0   side 0 */
    0x80A0U,    /* [1]  pull block       side 0 */
    0x6020U,    /* [2]  out x, 32        side 0 */
    0x6040U,    /* [3]  out y, 32        side 0 */
    0xE081U,    /* [4]  set pindirs, 1   side 0 */
    0x6001U,    /* [5]  out pins, 1      side 0 */
    0x1045U,    /* [6]  jmp x-- 5        side 1 */
    0xE080U,    /* [7]  set pindirs, 0   side 0 */
    0x006CU,    /* [8]  jmp !y 12        side 0 */
    0x5001U,    /* [9]  in pins, 1       side 1  (first sample at rise
                                                  RIGHT after PC=7 fall +
                                                  PC=8 low; catches chip's
                                                  bit 31 driven at PC=7
                                                  fall) */
    0x0089U,    /* [10] jmp y-- 9        side 0  (CLK falls; chip drives
                                                  next bit; loop) */
    0xA042U,    /* [11] nop              side 0 */
    0xC020U,    /* [12] irq wait 0       side 0 */
};
#define CYW43_GSPI_PROG_LEN \
    (sizeof(cyw43_gspi_program) / sizeof(cyw43_gspi_program[0]))
#define CYW43_GSPI_WRAP_TOP    12U
#define CYW43_GSPI_WRAP_BOTTOM 0U

/*---------------------------------------------------------------------------*/
/* PIO REGISTER HELPERS                                                      */
/*---------------------------------------------------------------------------*/

#define PIO1(off) (*(volatile uint32_t *)(CYW43_PIO_BASE + (off)))

/**
 * @brief Encode a PIO `set x, v` instruction
 *
 * Same idiom as the bitbang driver. SET takes a 5-bit immediate (0-31).
 *
 * @param v   Immediate value (0-31; masked to 5 bits)
 * @return Encoded 16-bit PIO instruction
 */
static inline uint16_t pio_instr_set_x(uint8_t v) {
    return (uint16_t)(0xE020U | (uint16_t)(v & 0x1FU));
}

/**
 * @brief Encode a PIO `set y, v` instruction
 *
 * Same idiom as the bitbang driver. SET takes a 5-bit immediate (0-31).
 *
 * @param v   Immediate value (0-31; masked to 5 bits)
 * @return Encoded 16-bit PIO instruction
 */
static inline uint16_t pio_instr_set_y(uint8_t v) {
    return (uint16_t)(0xE040U | (uint16_t)(v & 0x1FU));
}

/*---------------------------------------------------------------------------*/
/* WL_REG_ON (power gate)                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Drive WL_REG_ON (GP23) high or low to power on/off the CYW43 chip
 *
 * Configures the pad as a 4 mA SIO output, then sets or clears the
 * GP23 SIO bit. Driving high gates power to the chip; driving low
 * fully removes power so the chip resamples its mode-strap pins on
 * the next rising edge.
 *
 * @param high   Non-zero to drive the line high (chip powered),
 *               zero to drive it low (chip in reset)
 * @return TIKU_DRV_OK always
 */
static int wl_reg_on(int high)
{
    uint32_t bit = 1UL << WL_REG_ON_PIN;

    _RP2350_REG(RP2350_PADS_BANK0_GPIO(WL_REG_ON_PIN)) =
        RP2350_PADS_DRIVE_4MA;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(WL_REG_ON_PIN)) =
        RP2350_IO_FUNC_SIO;
    _RP2350_REG(RP2350_SIO_GPIO_OE_SET) = bit;

    if (high) {
        _RP2350_REG(RP2350_SIO_GPIO_OUT_SET) = bit;
    } else {
        _RP2350_REG(RP2350_SIO_GPIO_OUT_CLR) = bit;
    }
    return TIKU_DRV_OK;
}

/**
 * @brief Drive WL_DATA low briefly during chip power-up
 *
 * Matches the SDK's reset strap: WL_DATA is held low while WL_REG_ON
 * rises so the chip latches the correct mode at boot. Configured as
 * a SIO output with the 4 mA pad driver plus IE and pulldown enabled
 * so the pad has a defined level both before and after the SIO drive
 * takes effect.
 */
static void data_bootstrap_low_init(void)
{
    uint32_t bit = 1UL << WL_DATA_PIN;

    _RP2350_REG(RP2350_PADS_BANK0_GPIO(WL_DATA_PIN)) =
        RP2350_PADS_DRIVE_4MA | RP2350_PADS_IE | RP2350_PADS_PDE;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(WL_DATA_PIN)) =
        RP2350_IO_FUNC_SIO;
    _RP2350_REG(RP2350_SIO_GPIO_OUT_CLR) = bit;
    _RP2350_REG(RP2350_SIO_GPIO_OE_SET)  = bit;
}

/*---------------------------------------------------------------------------*/
/* WL_CS (chip select via SIO)                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise WL_CS (GP25) as a GPIO output for software-driven CS
 *
 * CS is driven directly by the CPU around each transaction rather than
 * folded into the PIO side-set, which keeps the firmware-upload window
 * trick available. Configures the pad with the 12 mA fast driver and
 * leaves CS idling high (deasserted) with output enabled.
 */
static void cs_init(void)
{
    uint32_t bit = 1UL << WL_CS_PIN;

    _RP2350_REG(RP2350_PADS_BANK0_GPIO(WL_CS_PIN)) =
        RP2350_PADS_DRIVE_12MA | RP2350_PADS_SLEWFAST;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(WL_CS_PIN)) =
        RP2350_IO_FUNC_SIO;
    /* Idle high, output enabled. */
    _RP2350_REG(RP2350_SIO_GPIO_OUT_SET) = bit;
    _RP2350_REG(RP2350_SIO_GPIO_OE_SET)  = bit;
}

/**
 * @brief Pull WL_CS low for the duration of a gSPI transaction
 */
static inline void cs_assert(void)
{
    _RP2350_REG(RP2350_SIO_GPIO_OUT_CLR) = 1UL << WL_CS_PIN;
}

/**
 * @brief Release WL_CS high between transactions
 */
static inline void cs_deassert(void)
{
    _RP2350_REG(RP2350_SIO_GPIO_OUT_SET) = 1UL << WL_CS_PIN;
}

/*---------------------------------------------------------------------------*/
/* PIO1 BUS PINS (mux WL_DATA + WL_CLOCK to PIO function)                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Hand WL_DATA + WL_CLOCK pin function over to PIO1
 *
 * Sets up the pad/IO mux so PIO1 owns both bus pins, and bypasses the
 * default GPIO input synchroniser on WL_DATA so the PIO state machine
 * sees pad level in the same cycle it samples.
 */
static void pio_pins_init(void)
{
    /* WL_DATA: input enable on (so PIO can sample during RX),
     * schmitt on for clean edges on the bidirectional line,
     * 12 mA fast drive when PIO drives it as output. Keep the SDK's
     * weak pulldown so the shared DATA/IRQ line has a defined idle
     * level while the chip is not driving it. */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(WL_DATA_PIN)) =
        RP2350_PADS_DRIVE_12MA |
        RP2350_PADS_IE         |
        RP2350_PADS_SCHMITT    |
        RP2350_PADS_SLEWFAST   |
        RP2350_PADS_PDE;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(WL_DATA_PIN)) =
        RP2350_IO_FUNC_PIO1;

    /* WL_CLOCK: output only. PIO drives the value via the SM's
     * side-set bit. We additionally force the pad's output enable
     * HIGH via IO_BANK0_GPIO_CTRL.OEOVER = 3 — that bypasses
     * PIO's per-SM PINDIRS register entirely, which is convenient
     * since the PIO program's own SET pindirs is routed to WL_DATA
     * (we need to be able to flip DATA between input and output
     * during the turnaround, so SET_BASE is pinned to WL_DATA).
     *
     * IMPORTANT — bit positions are different from RP2040. On
     * RP2350 the override fields shifted up by 4 bits:
     *   RP2040:  OUTOVER [9:8],   OEOVER [13:12]
     *   RP2350:  OUTOVER [13:12], OEOVER [15:14]
     * (3 << 12) on RP2350 = OUTOVER = 3 = force pad output value
     * HIGH, which silently freezes CLK at 1 and produces all-
     * 0xFFFFFFFF reads with no obvious symptom.
     *
     * OEOVER values: 0=normal/peripheral, 1=invert, 2=force LOW
     * (disable output), 3=force HIGH (force output enabled). */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(WL_CLOCK_PIN)) =
        RP2350_PADS_DRIVE_12MA | RP2350_PADS_SLEWFAST;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(WL_CLOCK_PIN)) =
        RP2350_IO_FUNC_PIO1 | (3UL << 14);   /* OEOVER = HIGH */

    /* The CYW43 data line is sampled relative to a PIO-generated
     * clock; bypass the default GPIO input synchronizer so PIO sees
     * the pad level in the same cycle it samples. */
    PIO1(RP2350_PIO_INPUT_SYNC_BYPASS) |= (1UL << WL_DATA_PIN);
}

/*---------------------------------------------------------------------------*/
/* PIO1 SM0 CONFIGURATION                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure PIO1 SM0 clkdiv/shiftctrl/execctrl/pinctrl for gSPI
 *
 * Programs CLKDIV (32.0 -> ~2.3 MHz wire clock, the sweet spot for
 * the CYW43439 turnaround timing), SHIFTCTRL (MSB-first OUT/IN with
 * AUTOPULL+AUTOPUSH at 32 bits), PINCTRL (WL_DATA as OUT/IN/SET base,
 * WL_CLOCK as side-set base), and EXECCTRL (wrap_top=12 / wrap_bot=0
 * so the SM loops back to the blocking pull after each transaction).
 */
static void sm_configure(void)
{
    uint32_t pinctrl;

    /* CLKDIV: 32.0 -> SM clock ≈ 4.7 MHz, wire clock ≈ 2.3 MHz.
     * At codex's previous 9.4 MHz wire clock (CLKDIV=8), the chip
     * misses its own MSB output bit during the DATA-pin
     * input-to-output turnaround — its driver can't settle in the
     * 53 ns SM-tick window, so sample 0 reads the chip's
     * still-high-Z line as 0 and the response comes back as
     * 0x3EADFEED (0xBEADFEED with bit 31 zeroed). At the prior
     * 0.6 MHz (CLKDIV=128) the chip's gSPI block doesn't drive at
     * all. 2.3 MHz wire (213 ns SM ticks) is the sweet spot
     * between "chip is responsive" and "chip MSB has time to
     * settle". */
    PIO1(RP2350_PIO_SM_CLKDIV(CYW43_SM)) = (32UL << 16);

    /* SHIFTCTRL:
     *   OUT_SHIFTDIR = LEFT  (bit 19 = 0)  — MSB-first onto DATA
     *   IN_SHIFTDIR  = LEFT  (bit 18 = 0)  — MSB-first into ISR
     *   AUTOPULL     = 1     (bit 17)      — drives the autopull-
     *                                        friendly `out X, 32`
     *                                        / `out Y, 32` pattern
     *                                        in the program; also
     *                                        lets write32 fetch
     *                                        its payload mid-loop
     *                                        without an explicit
     *                                        pull instruction
     *   AUTOPUSH     = 1     (bit 16)      — push ISR to RX FIFO
     *                                        once 32 bits have
     *                                        been shifted in
     *   PULL_THRESH  = 32    (encoded 0)
     *   PUSH_THRESH  = 32    (encoded 0)
     */
    PIO1(RP2350_PIO_SM_SHIFTCTRL(CYW43_SM)) =
        RP2350_PIO_SHIFTCTRL_AUTOPULL |
        RP2350_PIO_SHIFTCTRL_AUTOPUSH;

    /* PINCTRL:
     *   OUT_BASE  = WL_DATA   (data driven by host TX phase)
     *   IN_BASE   = WL_DATA   (data sampled by host RX phase)
     *   SET_BASE  = WL_DATA   (pindirs toggle during turnaround)
     *   SIDESET_BASE = WL_CLOCK
     *
     * The base fields are 5-bit GP indices; counts default to 0
     * and we set OUT/SET/SIDESET counts to 1 each (one pin per
     * group). */
    pinctrl =
        ((uint32_t)WL_DATA_PIN
            << RP2350_PIO_PINCTRL_OUT_BASE_SHIFT)        |
        ((uint32_t)WL_DATA_PIN
            << RP2350_PIO_PINCTRL_IN_BASE_SHIFT)         |
        ((uint32_t)WL_DATA_PIN
            << RP2350_PIO_PINCTRL_SET_BASE_SHIFT)        |
        ((uint32_t)WL_CLOCK_PIN
            << RP2350_PIO_PINCTRL_SIDESET_BASE_SHIFT)    |
        (1UL << RP2350_PIO_PINCTRL_OUT_COUNT_SHIFT)      |
        (1UL << RP2350_PIO_PINCTRL_SET_COUNT_SHIFT)      |
        (1UL << RP2350_PIO_PINCTRL_SIDESET_COUNT_SHIFT);
    PIO1(RP2350_PIO_SM_PINCTRL(CYW43_SM)) = pinctrl;

    /* EXECCTRL: WRAP_TOP=13 (the `irq wait` halt), WRAP_BOTTOM=0
     * (idle DATA=input after wrap-around). After PC=13's irq wait
     * completes (host clears flag 0), the SM tries to fetch PC=14;
     * the wrap diverts it to PC=0 and the program loops, ready for
     * the next host push.
     *
     * Layout: WRAP_TOP [16:12], WRAP_BOTTOM [11:7]. Other fields
     * left at reset defaults. */
    PIO1(RP2350_PIO_SM_EXECCTRL(CYW43_SM)) =
        ((uint32_t)CYW43_GSPI_WRAP_TOP    << 12) |
        ((uint32_t)CYW43_GSPI_WRAP_BOTTOM <<  7);
}

/**
 * @brief Load the gSPI PIO program into PIO1 instruction memory
 *
 * Copies the cyw43_gspi_program[] array verbatim into the PIO1 INSTR_MEM
 * region. Must be called before the SM is enabled.
 */
static void sm_load_program(void)
{
    uint8_t i;
    for (i = 0; i < (uint8_t)CYW43_GSPI_PROG_LEN; i++) {
        PIO1(RP2350_PIO_INSTR_MEM(i)) = cyw43_gspi_program[i];
    }
}

/**
 * @brief Execute a single PIO instruction without queuing
 *
 * Pokes the SM_INSTR register so the SM runs the given opcode on its
 * next clock, bypassing the program counter and FIFOs.
 *
 * @param instr   Encoded 16-bit PIO instruction to inject
 */
static inline void sm_force_exec(uint16_t instr)
{
    PIO1(RP2350_PIO_SM_INSTR(CYW43_SM)) = (uint32_t)instr;
}

/**
 * @brief Set the SM_ENABLE bit so PIO1 SM0 starts running
 */
static inline void sm_enable(void)
{
    PIO1(RP2350_PIO_CTRL) |= RP2350_PIO_CTRL_SM_ENABLE(CYW43_SM);
}

/**
 * @brief Disable and drain the SM state
 *
 * Clears SM_ENABLE then asserts SM_RESTART + CLKDIV_RESTART so any
 * mid-instruction state, shifter contents, and clock-divider phase
 * are wiped before the SM is re-enabled.
 */
static inline void sm_disable_restart(void)
{
    PIO1(RP2350_PIO_CTRL) &= ~RP2350_PIO_CTRL_SM_ENABLE(CYW43_SM);
    PIO1(RP2350_PIO_CTRL) |= RP2350_PIO_CTRL_SM_RESTART(CYW43_SM)
                          |  RP2350_PIO_CTRL_CLKDIV_RESTART(CYW43_SM);
}

/**
 * @brief Diagnostic: print PIO SM CTRL/FSTAT/FDEBUG
 *
 * Dump the PIO block's live diagnostic state via UART. Used when
 * the chip itself isn't physically accessible (Pico 2 W has the
 * CYW43 module shielded and GP24/25/29 aren't brought out to the
 * header), so all bus-state visibility has to come from the
 * RP2350's own observability registers:
 *
 *   PIO_CTRL    : which SMs are enabled
 *   PIO_FSTAT   : per-SM TX/RX FIFO full/empty flags (live)
 *   PIO_FDEBUG  : per-SM TX/RX stall and over/underrun flags
 *                 (sticky, W1C — survive until cleared by host)
 *   SM_ADDR     : current PC of the SM (read-only)
 *
 * FDEBUG bit layout (RP2350 datasheet § 11.7.3):
 *   [31:24] TXSTALL [SM3..SM0] — sticky, set when SM stalls on
 *                                PULL because TX FIFO empty
 *   [23:16] TXOVER  [SM3..SM0] — sticky, set on TX FIFO write
 *                                while full (push lost)
 *   [15:8]  RXUNDER [SM3..SM0] — sticky, set when SM stalls on
 *                                PUSH because RX FIFO full
 *   [7:0]   RXSTALL [SM3..SM0] — sticky, set when SM reads RX
 *                                FIFO that's empty
 *
 * @param tag   Short label prepended to the log line for context
 */
static void dump_pio_state(const char *tag)
{
    uint32_t ctrl   = PIO1(RP2350_PIO_CTRL);
    uint32_t fstat  = PIO1(RP2350_PIO_FSTAT);
    uint32_t fdebug = PIO1(RP2350_PIO_FDEBUG);
    uint32_t addr   = PIO1(RP2350_PIO_SM_ADDR(CYW43_SM));
    uint32_t irq    = PIO1(RP2350_PIO_IRQ);
    CYW43_PRINTF("  [%s] ctrl=0x%lx fstat=0x%lx fdebug=0x%lx "
                 "pc=%lu irq=0x%lx\n",
                 tag,
                 (unsigned long)ctrl,
                 (unsigned long)fstat,
                 (unsigned long)fdebug,
                 (unsigned long)addr,
                 (unsigned long)irq);
}

/*---------------------------------------------------------------------------*/
/* gSPI COMMAND-WORD BUILDER                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Pack a gSPI 32-bit command word
 *
 * Per the CYW43439 gSPI command structure:
 *
 *   bit 31     : C         (1 = write, 0 = read)
 *   bit 30     : A         (1 = auto-increment address)
 *   bits 29:28 : function  (0=bus, 1=backplane, 2=WLAN)
 *   bits 27:11 : address   (17 bits within the function space)
 *   bits 10:0  : byte count (11 bits; 0 encodes 2048 bytes)
 *
 * @param rw          1 = write, 0 = read
 * @param function    gSPI function select (0=bus, 1=backplane, 2=WLAN)
 * @param increment   1 = auto-increment address, 0 = fixed address
 * @param address     17-bit address within the function space
 * @param byte_count  Transfer length in bytes (11-bit field)
 * @return Packed 32-bit command word ready for the wire
 */
static uint32_t gspi_cmd_word(uint8_t rw, uint8_t function,
                              uint8_t increment,
                              uint32_t address, uint16_t byte_count)
{
    uint32_t w = 0UL;
    w |= ((uint32_t)(rw       & 0x01U)) << 31;
    w |= ((uint32_t)(increment & 0x01U)) << 30;
    w |= ((uint32_t)(function & 0x03U)) << 28;
    w |= ((uint32_t)(address  & 0x1FFFFU)) << 11;
    w |=  (uint32_t)(byte_count & 0x07FFU);
    return w;
}

/*---------------------------------------------------------------------------*/
/* SINGLE-TRANSACTION PRIMITIVE                                              */
/*---------------------------------------------------------------------------*/

/* FSTAT bit positions for SMn (RP2350 datasheet §11):
 *   bit 24+n: TXEMPTY[n]
 *   bit 16+n: TXFULL[n]
 *   bit  8+n: RXEMPTY[n]
 *   bit  0+n: RXFULL[n]
 */
#define CYW43_SM_TXEMPTY_BIT (1UL << (24U + CYW43_SM))
#define CYW43_SM_TXFULL_BIT  (1UL << (16U + CYW43_SM))
#define CYW43_SM_RXEMPTY_BIT (1UL << ( 8U + CYW43_SM))

/* Per-step spin budgets. Each loop iteration is ~10 CPU cycles at
 * 150 MHz; one wire word at 2.3 MHz takes ~14 µs = ~2100 cycles.
 * 5000 iterations (~50 µs) gives comfortable headroom for FIFO
 * head-of-line waits. The post-xfer IRQ wait keeps the original
 * 1 ms budget for backwards compat with single-word reads. */
#define CYW43_GSPI_FIFO_SPIN_LIMIT 5000UL

/**
 * @brief Run one gSPI transaction at the bit level
 *
 * One full gSPI transaction:
 *   1. Drain stale RX, clear sticky PIO flags.
 *   2. Assert CS.
 *   3. Push x_count, y_count, command, and optional payload.
 *   4. Spin until PIO has clocked payload/RX data and seen DATA high
 *      after reads.
 *   5. Drain `rx_words` data words.
 *   6. Clear PIO IRQ flag 0, then deassert CS.
 *
 * Returns TIKU_DRV_OK on success or TIKU_DRV_ERR_TIMEOUT if the
 * SM never raised IRQ within the spin budget — which is the
 * expected failure mode if the PIO state machine stalls.
 *
 * @param cmd_word      Packed 32-bit gSPI command word
 * @param tx            Pointer to TX payload words, or NULL if none
 * @param tx_data_bits  TX payload length in bits (must be a multiple of 32)
 * @param rx            Pointer to RX buffer, or NULL if rx_words == 0
 * @param rx_words      Number of 32-bit RX words to capture
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_INVALID on argument
 *         mismatch, TIKU_DRV_ERR_TIMEOUT if the SM stalls
 */
static int gspi_xfer_bits(uint32_t cmd_word,
                          const uint32_t *tx, uint32_t tx_data_bits,
                          uint32_t *rx, uint32_t rx_words)
{
    uint32_t spin;
    uint32_t txi, rxi;
    uint32_t tx_words;
    uint32_t tx_bits, rx_bits;
    int      verbose;

    if (!gspi.bus_up) {
        return TIKU_DRV_ERR_INVALID;
    }
    if ((tx_data_bits != 0U && tx == (const uint32_t *)0) ||
        (rx_words != 0U && rx == (uint32_t *)0)) {
        return TIKU_DRV_ERR_INVALID;
    }
    if ((tx_data_bits & 31U) != 0U) {
        /* The PIO program shifts in 32-bit chunks via AUTOPULL, so
         * we only support whole-word TX. Bit-level masking happens
         * upstream (e.g. byte cmd packing). */
        return TIKU_DRV_ERR_INVALID;
    }
    tx_words = tx_data_bits / 32U;

    /* Compute the program's count words. The SM loads X/Y with
     * `out x, 32` / `out y, 32` and decrements until zero, so each
     * count is (bits - 1). */
    tx_bits = 32U + tx_data_bits;
    rx_bits = rx_words * 32U;

    /* Per-transaction "[xfer OK]" log was useful during bring-up
     * but is a heavy UART load (one ~80-char line per gSPI xfer at
     * 115200 baud ≈ 7 ms blocking TX) — that monopolises CPU and
     * starves cooperative processes like the shell. Set this to 1
     * if you need to debug a stuck transfer. */
    verbose = 0;

    /* 1. Drain any stale RX FIFO data from a previous transaction. */
    while (!(PIO1(RP2350_PIO_FSTAT) & CYW43_SM_RXEMPTY_BIT)) {
        (void)PIO1(RP2350_PIO_RXF(CYW43_SM));
    }
    PIO1(RP2350_PIO_FDEBUG) = 0xFFFFFFFFUL;
    PIO1(RP2350_PIO_IRQ)    = 0xFFU;

    /* 2. Assert CS before pushing — SM unstalls on first push and
     * drives clocks immediately. */
    cs_assert();

    /* 3. Push x_count, y_count, cmd into TX FIFO. These three fit
     * without checking TXFULL — the SM consumes them as fast as we
     * push and the FIFO is 4 deep. */
    PIO1(RP2350_PIO_TXF(CYW43_SM)) = tx_bits - 1U;
    PIO1(RP2350_PIO_TXF(CYW43_SM)) =
        (rx_bits == 0U) ? 0U : (rx_bits - 1U);
    PIO1(RP2350_PIO_TXF(CYW43_SM)) = cmd_word;

    /* 4. Bidirectionally stream the rest: push TX while TX FIFO has
     * room, drain RX while RX FIFO has data. This lets large reads
     * (>4 words) work despite the 4-deep RX FIFO, and lets large
     * writes (>1 data word) work despite the 4-deep TX FIFO. */
    txi = 0U;
    rxi = 0U;
    spin = 0U;
    while (txi < tx_words || rxi < rx_words) {
        uint32_t fstat = PIO1(RP2350_PIO_FSTAT);
        int progress = 0;

        if (txi < tx_words && !(fstat & CYW43_SM_TXFULL_BIT)) {
            PIO1(RP2350_PIO_TXF(CYW43_SM)) = tx[txi++];
            progress = 1;
        }
        if (rxi < rx_words && !(fstat & CYW43_SM_RXEMPTY_BIT)) {
            rx[rxi++] = PIO1(RP2350_PIO_RXF(CYW43_SM));
            progress = 1;
        }
        if (progress) {
            spin = 0U;
            continue;
        }
        if (++spin > CYW43_GSPI_FIFO_SPIN_LIMIT) {
            dump_pio_state("xfer STREAM TIMEOUT");
            CYW43_PRINTF("  [stream] txi=%lu/%lu rxi=%lu/%lu fstat=0x%lx\n",
                         (unsigned long)txi, (unsigned long)tx_words,
                         (unsigned long)rxi, (unsigned long)rx_words,
                         (unsigned long)fstat);
            cs_deassert();
            return TIKU_DRV_ERR_TIMEOUT;
        }
    }

    /* 5. Wait for the SM to reach the irq-wait instruction. By now
     * all TX is consumed and all RX is drained, so this should fire
     * within ~1 wire cycle. */
    for (spin = 0; spin < CYW43_GSPI_SPIN_LIMIT; ++spin) {
        if (PIO1(RP2350_PIO_IRQ) & 0x01U) {
            break;
        }
    }
    if (spin >= CYW43_GSPI_SPIN_LIMIT) {
        dump_pio_state("xfer IRQ TIMEOUT");
        cs_deassert();
        return TIKU_DRV_ERR_TIMEOUT;
    }

    /* 6. Clear PIO IRQ flag — SM wraps to PC=0 and waits for next
     * x_count push. */
    PIO1(RP2350_PIO_IRQ) = 0x01U;

    if (verbose) {
        dump_pio_state("xfer OK");
    }
    cs_deassert();
    if (verbose) {
        uint32_t gpio_in = _RP2350_REG(RP2350_SIO_GPIO_IN);
        CYW43_PRINTF("  [post-xfer] DATA=%lu\n",
                     (unsigned long)((gpio_in >> WL_DATA_PIN) & 1U));
    }

    return TIKU_DRV_OK;
}

/**
 * @brief Higher-level wrapper around gspi_xfer_bits
 *
 * Handles direction (read/write) and word-count instead of bits, then
 * defers to gspi_xfer_bits for the actual transaction.
 *
 * @param cmd_word   Packed 32-bit gSPI command word
 * @param tx         Pointer to TX payload words, or NULL if tx_words == 0
 * @param tx_words   Number of 32-bit TX payload words
 * @param rx         Pointer to RX buffer, or NULL if rx_words == 0
 * @param rx_words   Number of 32-bit RX words to capture
 * @return Result of gspi_xfer_bits — TIKU_DRV_OK on success, error code
 *         otherwise
 */
static int gspi_xfer(uint32_t cmd_word,
                     const uint32_t *tx, uint32_t tx_words,
                     uint32_t *rx, uint32_t rx_words)
{
    return gspi_xfer_bits(cmd_word, tx, tx_words * 32U,
                          rx, rx_words);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

int cyw43_gspi_init(void)
{
    if (gspi.bus_up) {
        return TIKU_DRV_OK;
    }

    /* 1. CRITICAL: CS must be HIGH when WL_REG_ON rises. The
     * CYW43439 samples its CS_N pin at chip-boot (CHIP_PD's rising
     * edge) to choose between SPI mode (CS_N high) and SDIO mode
     * (CS_N low). If CS is uninitialised at WL_REG_ON's rising
     * edge, the chip can latch SDIO mode and silently ignore every
     * gSPI command we issue afterwards. Pre-set CS HIGH *before*
     * powering the chip.
     *
     * For the same reason, force WL_REG_ON low first to make sure
     * the chip is fully reset and resamples the mode pins on the
     * subsequent rising edge — not whatever residual state a warm
     * boot left behind. */
    (void)wl_reg_on(0);
    data_bootstrap_low_init();  /* match SDK reset strap: WL_DATA low */
    cs_init();                  /* CS -> HIGH (SPI-mode select) */
    tiku_common_delay_ms(20U);
    (void)wl_reg_on(1);
    gspi.powered = 1U;
    tiku_common_delay_ms(250U);

    /* 2. PIO1 out of reset; pins muxed; SM configured + program
     * loaded. SM is enabled ONCE here and stays enabled across all
     * transactions — the program wraps from PC=13 (irq wait) back
     * to PC=0, so the SM idles at the `pull block` stall with DATA
     * as input while waiting for the next transaction's inputs. */
    rp2350_unreset(RP2350_RESETS_PIO1);
    pio_pins_init();
    sm_load_program();
    sm_configure();

    /* Clear any stale PIO IRQ flags from prior boot state, then
     * drain the RX FIFO via FJOIN toggle before kicking the SM. */
    PIO1(RP2350_PIO_IRQ) = 0xFFU;
    {
        uint32_t sc = PIO1(RP2350_PIO_SM_SHIFTCTRL(CYW43_SM));
        PIO1(RP2350_PIO_SM_SHIFTCTRL(CYW43_SM)) =
            sc ^ RP2350_PIO_SHIFTCTRL_FJOIN_RX;
        PIO1(RP2350_PIO_SM_SHIFTCTRL(CYW43_SM)) = sc;
    }
    sm_enable();

    gspi.bus_up = 1U;
    gspi.configured_32 = 0U;

    CYW43_PRINTF("gspi_init: power on, PIO1 SM0 configured, "
                 "WL_CLOCK forced output via OEOVER, SM enabled\n");

    /* Pin-state sanity check via GPIO_IN. The PADS register has IE
     * enabled for WL_DATA but not for WL_CS/WL_CLOCK/WL_REG_ON, so
     * the readback below only reflects the actual pin voltage for
     * WL_DATA — for the outputs we also flip IE on briefly so we
     * can see what's actually on the wire vs what we drove. */
    {
        uint32_t gpio_in;
        uint32_t mask = (1UL << WL_REG_ON_PIN) | (1UL << WL_DATA_PIN)
                      | (1UL << WL_CS_PIN)     | (1UL << WL_CLOCK_PIN);
        /* Briefly enable input on all four for a readback. */
        _RP2350_REG(RP2350_PADS_BANK0_GPIO(WL_REG_ON_PIN)) =
            RP2350_PADS_DRIVE_4MA | RP2350_PADS_IE;
        _RP2350_REG(RP2350_PADS_BANK0_GPIO(WL_CS_PIN)) =
            RP2350_PADS_DRIVE_4MA | RP2350_PADS_IE;
        _RP2350_REG(RP2350_PADS_BANK0_GPIO(WL_CLOCK_PIN)) =
            RP2350_PADS_DRIVE_4MA | RP2350_PADS_IE;
        gpio_in = _RP2350_REG(RP2350_SIO_GPIO_IN);
        CYW43_PRINTF("pin readback: gpio_in=0x%lx masked=0x%lx "
                     "REG_ON=%lu DATA=%lu CS=%lu CLK=%lu\n",
                     (unsigned long)gpio_in,
                     (unsigned long)(gpio_in & mask),
                     (unsigned long)((gpio_in >> WL_REG_ON_PIN) & 1U),
                     (unsigned long)((gpio_in >> WL_DATA_PIN)   & 1U),
                     (unsigned long)((gpio_in >> WL_CS_PIN)     & 1U),
                     (unsigned long)((gpio_in >> WL_CLOCK_PIN)  & 1U));
    }
    return TIKU_DRV_OK;
}

void cyw43_gspi_deinit(void)
{
    if (gspi.bus_up) {
        sm_disable_restart();
        cs_deassert();
    }
    (void)wl_reg_on(0);

    gspi.bus_up  = 0U;
    gspi.powered = 0U;
    gspi.ready   = 0U;
    gspi.configured_32 = 0U;
}

static uint32_t gspi_swap16(uint32_t v);

int cyw43_gspi_read32(uint8_t function, uint32_t address,
                      uint32_t *out_value)
{
    uint32_t cmd, rx_word = 0UL;
    int      rc;

    if (out_value == (uint32_t *)0) {
        return TIKU_DRV_ERR_INVALID;
    }

    /* Two wire formats depending on chip state:
     *
     *   - Before BUS_CTRL is configured (gspi.configured_32 == 0)
     *     the chip is in its reset-default 16-bit-LE word format,
     *     so cmd and response need halfword-swap on the host side.
     *   - After the BUS_CTRL write that sets WORD_LENGTH_32 +
     *     ENDIAN_BIG, the chip switches to natural 32-bit MSB-first
     *     wire order and no swap is needed.
     */
    cmd = gspi_cmd_word(0U, function, 1U, address, 4U);
    if (!gspi.configured_32) {
        cmd = gspi_swap16(cmd);
    }
    rc = gspi_xfer(cmd, (const uint32_t *)0, 0U, &rx_word, 1U);
    if (rc == TIKU_DRV_OK) {
        *out_value = gspi.configured_32 ? rx_word : gspi_swap16(rx_word);
    }
    return rc;
}

static uint32_t gspi_swap16(uint32_t v)
{
    return (v << 16) | (v >> 16);
}

static uint32_t gspi_rev16x2(uint32_t v)
{
    return ((v & 0x00FF00FFUL) << 8) | ((v & 0xFF00FF00UL) >> 8);
}

static uint32_t gspi_bswap32(uint32_t v)
{
    return ((v & 0x000000FFUL) << 24) |
           ((v & 0x0000FF00UL) <<  8) |
           ((v & 0x00FF0000UL) >>  8) |
           ((v & 0xFF000000UL) >> 24);
}

static int gspi_read32_encoded(uint32_t encoded_cmd, uint32_t *raw_value)
{
    if (raw_value == (uint32_t *)0) {
        return TIKU_DRV_ERR_INVALID;
    }
    return gspi_xfer(encoded_cmd, (const uint32_t *)0, 0U, raw_value, 1U);
}

int cyw43_gspi_write32(uint8_t function, uint32_t address,
                       uint32_t value)
{
    uint32_t cmd = gspi_cmd_word(1U, function, 1U, address, 4U);
    uint32_t tx  = value;

    /* Same wire-format gate as cyw43_gspi_read32. */
    if (!gspi.configured_32) {
        cmd = gspi_swap16(cmd);
        tx  = gspi_swap16(tx);
    }
    return gspi_xfer(cmd, &tx, 1U, (uint32_t *)0, 0U);
}

static int gspi_read32_swapped(uint8_t function, uint32_t address,
                               uint32_t *out_value)
{
    uint32_t cmd, rx_word = 0UL;
    int      rc;

    if (out_value == (uint32_t *)0) {
        return TIKU_DRV_ERR_INVALID;
    }

    cmd = gspi_swap16(gspi_cmd_word(0U, function, 1U, address, 4U));
    rc  = gspi_xfer(cmd, (const uint32_t *)0, 0U, &rx_word, 1U);
    if (rc == TIKU_DRV_OK) {
        *out_value = gspi_swap16(rx_word);
    }
    return rc;
}

static int gspi_write32_swapped(uint8_t function, uint32_t address,
                                uint32_t value)
{
    uint32_t cmd = gspi_swap16(gspi_cmd_word(1U, function, 1U, address, 4U));
    uint32_t tx  = gspi_swap16(value);

    return gspi_xfer(cmd, &tx, 1U, (uint32_t *)0, 0U);
}

/*---------------------------------------------------------------------------*/
/* BYTE-WIDE READ/WRITE + BACKPLANE WINDOW                                   */
/*---------------------------------------------------------------------------*/
/*
 * Byte-level access matches embassy-rs's read8/write8: the cmd word
 * carries byte_count=1, and on the wire the chip still consumes/
 * produces a full 32-bit value word with the byte in its LSB position
 * (the chip's reset register layout is little-endian — byte 0 of the
 * register sits at value bit 7..0).
 *
 * Backplane reads include the configured response-delay padding word,
 * so they fetch two 32-bit words and use the second; non-backplane
 * reads use a single word.
 */
static uint32_t bp_window_cache = 0xAAAAAAAAUL;  /* impossible value */

/**
 * @brief Program SBADDR registers so the backplane points at a 32 KB window
 *
 * Caches the most-recently-set window value and short-circuits the SPI
 * write when the chip already points at the requested 32 KB region.
 * Otherwise packs bits 8..31 of the window base into a single 32-bit
 * write to F1/SBADDR_LOW (the chip stores the value little-endian into
 * SBADDR_LOW/MID/HIGH).
 *
 * @param bp_addr   Any backplane address inside the desired 32 KB window
 * @return TIKU_DRV_OK on success or after a cache hit, otherwise the
 *         error code from cyw43_gspi_write32
 */
static int bp_set_window(uint32_t bp_addr)
{
    uint32_t new_window = bp_addr & ~GSPI_BACKPLANE_ADDRESS_MASK;
    uint32_t v;
    int      rc;

    if (new_window == bp_window_cache) {
        return TIKU_DRV_OK;
    }

    /* Pack bits 8..31 of new_window into a single 32-bit write to
     * F1/SBADDR_LOW (= 0x1000A) with addr-increment. The chip stores
     * register bytes little-endian, so:
     *   value byte 0 -> reg 0x1000A (SBADDR_LOW  = new_window bits 15:8)
     *   value byte 1 -> reg 0x1000B (SBADDR_MID  = new_window bits 23:16)
     *   value byte 2 -> reg 0x1000C (SBADDR_HIGH = new_window bits 31:24)
     */
    v  = (new_window >> 8) & 0x00FFFFFFUL;

    rc = cyw43_gspi_write32(GSPI_FUNCTION_BACKPLANE,
                            GSPI_REG_SBADDR_LOW, v);
    if (rc == TIKU_DRV_OK) {
        bp_window_cache = new_window;
    }
    return rc;
}

int cyw43_gspi_read8(uint8_t function, uint32_t address,
                     uint8_t *out_value)
{
    uint32_t cmd;
    uint32_t buf[2] = { 0UL, 0UL };
    uint16_t rx_words;
    int      rc;

    if (out_value == (uint8_t *)0) {
        return TIKU_DRV_ERR_INVALID;
    }

    cmd      = gspi_cmd_word(0U, function, 1U, address, 1U);
    rx_words = (function == GSPI_FUNCTION_BACKPLANE) ? 2U : 1U;

    if (!gspi.configured_32) {
        cmd = gspi_swap16(cmd);
    }

    rc = gspi_xfer(cmd, (const uint32_t *)0, 0U, buf, rx_words);
    if (rc == TIKU_DRV_OK) {
        uint32_t v = (function == GSPI_FUNCTION_BACKPLANE) ? buf[1] : buf[0];
        if (!gspi.configured_32) {
            v = gspi_swap16(v);
        }
        *out_value = (uint8_t)(v & 0xFFU);
    }
    return rc;
}

int cyw43_gspi_write8(uint8_t function, uint32_t address, uint8_t value)
{
    uint32_t cmd = gspi_cmd_word(1U, function, 1U, address, 1U);
    uint32_t tx  = (uint32_t)value;

    if (!gspi.configured_32) {
        cmd = gspi_swap16(cmd);
        tx  = gspi_swap16(tx);
    }
    return gspi_xfer(cmd, &tx, 1U, (uint32_t *)0, 0U);
}

int cyw43_gspi_bp_read8(uint32_t bp_addr, uint8_t *out_value)
{
    int rc = bp_set_window(bp_addr);
    if (rc != TIKU_DRV_OK) {
        return rc;
    }
    return cyw43_gspi_read8(GSPI_FUNCTION_BACKPLANE,
                            bp_addr & GSPI_BACKPLANE_ADDRESS_MASK,
                            out_value);
}

int cyw43_gspi_bp_write8(uint32_t bp_addr, uint8_t value)
{
    int rc = bp_set_window(bp_addr);
    if (rc != TIKU_DRV_OK) {
        return rc;
    }
    return cyw43_gspi_write8(GSPI_FUNCTION_BACKPLANE,
                             bp_addr & GSPI_BACKPLANE_ADDRESS_MASK,
                             value);
}

int cyw43_gspi_bp_read32(uint32_t bp_addr, uint32_t *out_value)
{
    uint32_t cmd, bus_addr;
    uint32_t buf[2] = { 0UL, 0UL };
    int      rc;

    if (out_value == (uint32_t *)0) {
        return TIKU_DRV_ERR_INVALID;
    }
    rc = bp_set_window(bp_addr);
    if (rc != TIKU_DRV_OK) {
        return rc;
    }
    bus_addr = (bp_addr & GSPI_BACKPLANE_ADDRESS_MASK)
             | GSPI_BACKPLANE_ADDRESS_32B_FLAG;
    cmd = gspi_cmd_word(0U, GSPI_FUNCTION_BACKPLANE, 1U, bus_addr, 4U);
    if (!gspi.configured_32) {
        cmd = gspi_swap16(cmd);
    }
    rc = gspi_xfer(cmd, (const uint32_t *)0, 0U, buf, 2U);
    if (rc == TIKU_DRV_OK) {
        *out_value = gspi.configured_32 ? buf[1] : gspi_swap16(buf[1]);
    }
    return rc;
}

int cyw43_gspi_bp_write32(uint32_t bp_addr, uint32_t value)
{
    uint32_t cmd, bus_addr, tx;
    int      rc = bp_set_window(bp_addr);
    if (rc != TIKU_DRV_OK) {
        return rc;
    }
    bus_addr = (bp_addr & GSPI_BACKPLANE_ADDRESS_MASK)
             | GSPI_BACKPLANE_ADDRESS_32B_FLAG;
    cmd = gspi_cmd_word(1U, GSPI_FUNCTION_BACKPLANE, 1U, bus_addr, 4U);
    tx  = value;
    if (!gspi.configured_32) {
        cmd = gspi_swap16(cmd);
        tx  = gspi_swap16(tx);
    }
    return gspi_xfer(cmd, &tx, 1U, (uint32_t *)0, 0U);
}

#define GSPI_BLOCK_MAX_BYTES 64U
#define GSPI_BLOCK_MAX_WORDS ((GSPI_BLOCK_MAX_BYTES + 3U) / 4U)

/**
 * @brief Read or write a contiguous block on the chip's backplane
 *
 * Block I/O. The cmd word's byte_count field is 11 bits (max 2047),
 * but each transaction is bounded to GSPI_BLOCK_MAX_BYTES = 64 — same
 * as embassy-rs's BACKPLANE_MAX_TRANSFER_SIZE — so the streaming TX
 * path never has to refill more than 16 data words per CS pulse.
 *
 * Byte ordering: bytes are packed into 32-bit words using the host's
 * little-endian layout (data[0] in LSB, data[3] in MSB). The PIO
 * sends each u32 MSB-first on the wire; the chip is configured in
 * 32-bit-word LE mode (no ENDIAN_BIG bit), so chip's register byte 0
 * = u32 LSB byte = data[0]. INC_ADDR means successive bytes land at
 * successive register addresses, exactly matching `data[]` order.
 *
 * The cmd word's byte_count tells the chip how many register-byte
 * cycles to perform; on a non-multiple-of-4 length, the trailing
 * 1..3 wire bytes are written/ignored at unused register addresses
 * past the count. Pad bytes (write) / discarded bytes (read) live in
 * those positions.
 *
 * @param function    gSPI function select (BUS / BACKPLANE / WLAN)
 * @param bus_addr    Function-space address for the first byte
 * @param tx_bytes    Source buffer for writes, NULL for reads
 * @param rx_bytes    Destination buffer for reads, NULL for writes
 * @param byte_count  Number of bytes to transfer (1..GSPI_BLOCK_MAX_BYTES)
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_INVALID for a bad length,
 *         or the error code from the underlying gspi_xfer
 */
static int gspi_block_xfer(uint8_t function, uint32_t bus_addr,
                           const uint8_t *tx_bytes, uint8_t *rx_bytes,
                           uint32_t byte_count)
{
    static uint32_t tx_buf[GSPI_BLOCK_MAX_WORDS];
    static uint32_t rx_buf[GSPI_BLOCK_MAX_WORDS + 1U]; /* +1 = bp pad */
    uint32_t cmd;
    uint32_t word_count;
    uint32_t i;
    int      rc;

    if (byte_count == 0U || byte_count > GSPI_BLOCK_MAX_BYTES) {
        return TIKU_DRV_ERR_INVALID;
    }
    word_count = (byte_count + 3U) / 4U;

    if (tx_bytes != (const uint8_t *)0) {
        for (i = 0U; i < word_count; ++i) {
            uint32_t w = 0UL;
            uint32_t b0 = (i * 4U + 0U < byte_count)
                              ? (uint32_t)tx_bytes[i * 4U + 0U] : 0UL;
            uint32_t b1 = (i * 4U + 1U < byte_count)
                              ? (uint32_t)tx_bytes[i * 4U + 1U] : 0UL;
            uint32_t b2 = (i * 4U + 2U < byte_count)
                              ? (uint32_t)tx_bytes[i * 4U + 2U] : 0UL;
            uint32_t b3 = (i * 4U + 3U < byte_count)
                              ? (uint32_t)tx_bytes[i * 4U + 3U] : 0UL;
            w = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
            tx_buf[i] = w;
        }
    }

    cmd = gspi_cmd_word((tx_bytes != (const uint8_t *)0) ? 1U : 0U,
                        function, 1U, bus_addr, (uint16_t)byte_count);

    if (tx_bytes != (const uint8_t *)0) {
        rc = gspi_xfer(cmd, tx_buf, word_count, (uint32_t *)0, 0U);
    } else {
        uint32_t rx_words = word_count;
        if (function == GSPI_FUNCTION_BACKPLANE) {
            rx_words += 1U;     /* prepended response-delay word */
        }
        rc = gspi_xfer(cmd, (const uint32_t *)0, 0U, rx_buf, rx_words);
    }
    if (rc != TIKU_DRV_OK) {
        return rc;
    }

    if (rx_bytes != (uint8_t *)0) {
        const uint32_t *src = rx_buf;
        if (function == GSPI_FUNCTION_BACKPLANE) {
            src = &rx_buf[1];   /* skip padding word */
        }
        for (i = 0U; i < byte_count; ++i) {
            rx_bytes[i] = (uint8_t)((src[i / 4U] >> ((i & 3U) * 8U))
                                    & 0xFFU);
        }
    }

    return TIKU_DRV_OK;
}

int cyw43_gspi_read_block(uint8_t function, uint32_t address,
                          uint8_t *buf, size_t byte_count)
{
    if (buf == (uint8_t *)0 || byte_count == 0U) {
        return TIKU_DRV_ERR_INVALID;
    }
    while (byte_count > 0U) {
        uint32_t chunk = (byte_count > GSPI_BLOCK_MAX_BYTES)
                            ? GSPI_BLOCK_MAX_BYTES : (uint32_t)byte_count;
        int rc = gspi_block_xfer(function, address,
                                 (const uint8_t *)0, buf, chunk);
        if (rc != TIKU_DRV_OK) {
            return rc;
        }
        buf        += chunk;
        address    += chunk;
        byte_count -= chunk;
    }
    return TIKU_DRV_OK;
}

int cyw43_gspi_write_block(uint8_t function, uint32_t address,
                           const uint8_t *buf, size_t byte_count)
{
    if (buf == (const uint8_t *)0 || byte_count == 0U) {
        return TIKU_DRV_ERR_INVALID;
    }
    while (byte_count > 0U) {
        uint32_t chunk = (byte_count > GSPI_BLOCK_MAX_BYTES)
                            ? GSPI_BLOCK_MAX_BYTES : (uint32_t)byte_count;
        int rc = gspi_block_xfer(function, address, buf,
                                 (uint8_t *)0, chunk);
        if (rc != TIKU_DRV_OK) {
            return rc;
        }
        buf        += chunk;
        address    += chunk;
        byte_count -= chunk;
    }
    return TIKU_DRV_OK;
}

int cyw43_gspi_bp_write(uint32_t bp_addr, const uint8_t *data,
                        size_t byte_count)
{
    if (data == (const uint8_t *)0 || byte_count == 0U) {
        return TIKU_DRV_ERR_INVALID;
    }
    while (byte_count > 0U) {
        uint32_t window_offs    = bp_addr & GSPI_BACKPLANE_ADDRESS_MASK;
        uint32_t window_remain  = (GSPI_BACKPLANE_ADDRESS_MASK + 1UL)
                                  - window_offs;
        uint32_t chunk          = (byte_count > GSPI_BLOCK_MAX_BYTES)
                                     ? GSPI_BLOCK_MAX_BYTES
                                     : (uint32_t)byte_count;
        int rc;

        if (chunk > window_remain) {
            chunk = window_remain;
        }

        rc = bp_set_window(bp_addr);
        if (rc != TIKU_DRV_OK) return rc;

        rc = gspi_block_xfer(GSPI_FUNCTION_BACKPLANE, window_offs,
                             data, (uint8_t *)0, chunk);
        if (rc != TIKU_DRV_OK) return rc;

        bp_addr    += chunk;
        data       += chunk;
        byte_count -= chunk;
    }
    return TIKU_DRV_OK;
}

/*---------------------------------------------------------------------------*/
/* CORE CONTROL (disable / reset via AI wrapper registers)                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Issue WRAPPER ioctrl/resetctrl writes to disable a chip core
 *
 * Mirrors embassy-rs's chip::disable_device_core. `base` is the AI
 * wrapper base (e.g. GSPI_BACKPLANE_ARM_CORE_BASE), `halt` selects
 * the CPU-halt path for the ARM core (the SOCSRAM core never needs
 * it). All accesses go through the byte-level backplane API.
 *
 * If the core is already in reset, returns OK without touching ioctrl;
 * otherwise drives ioctrl with CPUHALT (when requested), waits 1 ms,
 * then asserts resetctrl's reset bit and waits another 1 ms.
 *
 * @param base   AI wrapper base address of the target core
 * @param halt   Non-zero to assert CPUHALT (ARM core only)
 * @param name   Short label used in diagnostic prints
 * @return TIKU_DRV_OK on success, or the error code from the
 *         underlying backplane read/write that failed
 */
static int gspi_disable_core(uint32_t base, uint8_t halt, const char *name)
{
    uint8_t  v;
    int      rc;
    uint8_t  ioctrl = halt ? GSPI_AI_IOCTRL_BIT_CPUHALT : 0U;

    /* Embassy reads resetctrl twice (first is a discarded warm-up
     * read in the original WHD code). Drop one — our PIO doesn't
     * suffer the same wrapper-quirk that motivated it. */
    rc = cyw43_gspi_bp_read8(base + GSPI_AI_RESETCTRL_OFFSET, &v);
    if (rc != TIKU_DRV_OK) {
        CYW43_PRINTF("p2.C: %s read RESETCTRL failed rc=%d\n", name, rc);
        return rc;
    }
    if (v & GSPI_AI_RESETCTRL_BIT_RESET) {
        CYW43_PRINTF("p2.C: %s already in reset (resetctrl=0x%02x)\n",
                     name, v);
        return TIKU_DRV_OK;
    }

    rc = cyw43_gspi_bp_write8(base + GSPI_AI_IOCTRL_OFFSET, ioctrl);
    if (rc != TIKU_DRV_OK) return rc;
    rc = cyw43_gspi_bp_read8(base + GSPI_AI_IOCTRL_OFFSET, &v);
    if (rc != TIKU_DRV_OK) return rc;

    tiku_common_delay_ms(1U);

    rc = cyw43_gspi_bp_write8(base + GSPI_AI_RESETCTRL_OFFSET,
                              GSPI_AI_RESETCTRL_BIT_RESET);
    if (rc != TIKU_DRV_OK) return rc;

    tiku_common_delay_ms(1U);

    CYW43_PRINTF("p2.C: %s disabled (final ioctrl readback=0x%02x)\n",
                 name, v);
    return TIKU_DRV_OK;
}

/**
 * @brief Disable, reset, and re-enable a chip core (idempotent power-up)
 *
 * Mirrors embassy-rs's chip::reset_device_core. Calls gspi_disable_core
 * first, then drives ioctrl with FGC+CLOCK_EN (and CPUHALT if `halt`),
 * deasserts resetctrl, waits 1 ms, then clears FGC leaving CLOCK_EN
 * (and CPUHALT if `halt`) set.
 *
 * @param base   AI wrapper base address of the target core
 * @param halt   Non-zero to keep CPUHALT asserted across the sequence
 *               (ARM core when firmware is not yet uploaded)
 * @param name   Short label used in diagnostic prints
 * @return TIKU_DRV_OK on success, or the first failing rc from any
 *         underlying backplane read/write
 */
static int gspi_reset_core(uint32_t base, uint8_t halt, const char *name)
{
    uint8_t v;
    int     rc;
    uint8_t ioctrl_rel = (uint8_t)(halt ? (GSPI_AI_IOCTRL_BIT_CPUHALT |
                                           GSPI_AI_IOCTRL_BIT_FGC     |
                                           GSPI_AI_IOCTRL_BIT_CLOCK_EN)
                                        : (GSPI_AI_IOCTRL_BIT_FGC     |
                                           GSPI_AI_IOCTRL_BIT_CLOCK_EN));
    uint8_t ioctrl_run = (uint8_t)(halt ? (GSPI_AI_IOCTRL_BIT_CPUHALT |
                                           GSPI_AI_IOCTRL_BIT_CLOCK_EN)
                                        : GSPI_AI_IOCTRL_BIT_CLOCK_EN);

    rc = gspi_disable_core(base, halt, name);
    if (rc != TIKU_DRV_OK) {
        return rc;
    }

    rc = cyw43_gspi_bp_write8(base + GSPI_AI_IOCTRL_OFFSET, ioctrl_rel);
    if (rc != TIKU_DRV_OK) return rc;
    rc = cyw43_gspi_bp_read8(base + GSPI_AI_IOCTRL_OFFSET, &v);
    if (rc != TIKU_DRV_OK) return rc;

    rc = cyw43_gspi_bp_write8(base + GSPI_AI_RESETCTRL_OFFSET, 0U);
    if (rc != TIKU_DRV_OK) return rc;

    tiku_common_delay_ms(1U);

    rc = cyw43_gspi_bp_write8(base + GSPI_AI_IOCTRL_OFFSET, ioctrl_run);
    if (rc != TIKU_DRV_OK) return rc;
    rc = cyw43_gspi_bp_read8(base + GSPI_AI_IOCTRL_OFFSET, &v);
    if (rc != TIKU_DRV_OK) return rc;

    tiku_common_delay_ms(1U);

    CYW43_PRINTF("p2.C: %s reset (final ioctrl readback=0x%02x)\n", name, v);
    return TIKU_DRV_OK;
}

/**
 * @brief Check WRAPPER resetctrl/ioctrl to see if a core is running
 *
 * Reads the AI wrapper's ioctrl and resetctrl registers and verifies
 * that the clock is enabled (no FGC, CLOCK_EN set) and the core is
 * not held in reset. Logs the relevant register values either way.
 *
 * @param base   AI wrapper base address of the target core
 * @param name   Short label used in diagnostic prints
 * @return TIKU_DRV_OK if the core is up, TIKU_DRV_ERR_NOT_PRESENT if
 *         ioctrl/resetctrl indicate it is still gated or held in reset,
 *         or the error code from a failing backplane read
 */
static int gspi_core_is_up(uint32_t base, const char *name)
{
    uint8_t io = 0U, rs = 0U;
    int     rc;

    rc = cyw43_gspi_bp_read8(base + GSPI_AI_IOCTRL_OFFSET, &io);
    if (rc != TIKU_DRV_OK) return rc;
    rc = cyw43_gspi_bp_read8(base + GSPI_AI_RESETCTRL_OFFSET, &rs);
    if (rc != TIKU_DRV_OK) return rc;

    if ((io & (GSPI_AI_IOCTRL_BIT_FGC | GSPI_AI_IOCTRL_BIT_CLOCK_EN))
        != GSPI_AI_IOCTRL_BIT_CLOCK_EN) {
        CYW43_PRINTF("p2.C: %s NOT up — ioctrl=0x%02x\n", name, io);
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    if (rs & GSPI_AI_RESETCTRL_BIT_RESET) {
        CYW43_PRINTF("p2.C: %s NOT up — resetctrl=0x%02x\n", name, rs);
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    CYW43_PRINTF("p2.C: %s up (ioctrl=0x%02x resetctrl=0x%02x)\n",
                 name, io, rs);
    return TIKU_DRV_OK;
}

/**
 * @brief Toggle WL_REG_ON low for ~10 ms to fully power-cycle the chip
 *
 * Drives WL_REG_ON low (with CS pre-released and the WL_DATA bootstrap
 * strap reasserted), waits long enough for the chip's internal rails
 * to discharge, then powers it back up and re-muxes the PIO pins.
 * Used at the start of the bus probe so the chip resamples its
 * mode-strap pins on the rising edge of WL_REG_ON. Also resets the
 * driver's `configured_32` / `ready` shadow flags.
 */
static void gspi_chip_power_cycle(void)
{
    cs_deassert();
    (void)wl_reg_on(0);
    data_bootstrap_low_init();
    gspi.configured_32 = 0U;
    gspi.ready = 0U;
    tiku_common_delay_ms(20U);
    (void)wl_reg_on(1);
    tiku_common_delay_ms(250U);
    pio_pins_init();
}

/*---------------------------------------------------------------------------*/
/* F2 PACKET PRIMITIVES                                                      */
/*---------------------------------------------------------------------------*/
/*
 * F2 (FUNC_WLAN) carries the WHD packet path. Unlike F1 backplane
 * reads, F2 reads have NO response-delay padding word: the cmd word
 * is followed directly by N data words.
 *
 * Caller supplies the destination buffer. The protocol layer
 * (whd.c) owns its 2 KB SDPCM frame buffer; we just stream bytes
 * to/from it.
 */

int cyw43_gspi_f2_rx_try(uint32_t *buf, uint32_t buf_words,
                         uint32_t *out_byte_count)
{
    uint32_t status, pkt_len, word_count, cmd;
    int      rc;

    *out_byte_count = 0U;
    rc = cyw43_gspi_read32(GSPI_FUNCTION_BUS,
                           GSPI_REG_SPI_STATUS, &status);
    if (rc != TIKU_DRV_OK) return rc;
    if (!(status & GSPI_STATUS_F2_PKT_AVAILABLE)) {
        return TIKU_DRV_OK;
    }

    pkt_len = (status & GSPI_STATUS_F2_PKT_LEN_MASK)
              >> GSPI_STATUS_F2_PKT_LEN_SHIFT;
    if (pkt_len == 0U || pkt_len > buf_words * 4U) {
        CYW43_PRINTF("f2_rx: bad pkt_len %lu STATUS=0x%08lx\n",
                     (unsigned long)pkt_len, (unsigned long)status);
        return TIKU_DRV_ERR_INVALID;
    }

    word_count = (pkt_len + 3U) / 4U;
    cmd = gspi_cmd_word(0U, GSPI_FUNCTION_WLAN, 1U, 0UL,
                        (uint16_t)pkt_len);
    rc = gspi_xfer(cmd, (const uint32_t *)0, 0U, buf, word_count);
    if (rc != TIKU_DRV_OK) return rc;
    *out_byte_count = pkt_len;
    return TIKU_DRV_OK;
}

int cyw43_gspi_f2_rx_wait(uint32_t *buf, uint32_t buf_words,
                          uint32_t *out_byte_count, uint32_t timeout_ms)
{
    uint32_t i;
    int      rc;
    for (i = 0U; i < timeout_ms; ++i) {
        rc = cyw43_gspi_f2_rx_try(buf, buf_words, out_byte_count);
        if (rc != TIKU_DRV_OK) return rc;
        if (*out_byte_count > 0U) return TIKU_DRV_OK;
        tiku_common_delay_ms(1U);
    }
    return TIKU_DRV_ERR_TIMEOUT;
}

int cyw43_gspi_f2_tx(const uint32_t *tx_words, uint32_t byte_count)
{
    uint32_t word_count = (byte_count + 3U) / 4U;
    uint32_t cmd = gspi_cmd_word(1U, GSPI_FUNCTION_WLAN, 1U, 0UL,
                                 (uint16_t)byte_count);
    return gspi_xfer(cmd, tx_words, word_count, (uint32_t *)0, 0U);
}

/*---------------------------------------------------------------------------*/
/* BUS-ALIVE PROBE (phase 1 acceptance gate, simplified)                     */
/*---------------------------------------------------------------------------*/

/*
 * Real chip-ID (0xA9A6) lives in chipcommon behind the backplane.
 * Phase 1 only proves that the gSPI bus-function registers are
 * readable, so the acceptance read is SPI_TEST_RO at F0/0x14. The
 * CYW43439 datasheet defines that register as the fixed pattern
 * 0xFEEDBEAD.
 */
int cyw43_gspi_probe_chip_id(void)
{
    uint32_t boot_cmd;
    uint32_t raw = 0UL;
    uint32_t v = 0UL;
    int      rc;
    unsigned int i;
    static const struct {
        const char *name;
        uint32_t (*encode)(uint32_t);
        uint32_t (*decode)(uint32_t);
    } boot_encodings[] = {
        { "rot16/sdk-wire", gspi_swap16,  gspi_swap16  },
        { "raw",            0,            0            },
        { "rev16x2",        gspi_rev16x2, gspi_rev16x2 },
        { "bswap32",        gspi_bswap32, gspi_bswap32 },
    };

    /* Start from the chip's reset-default SPI framing. CYW43439 answers
     * F0 registers in 16-bit word mode after WL_REG_ON; both command and
     * payload/status words need their halfwords rotated until BUS_CTRL
     * sets WORD_LENGTH_32. */
    gspi_chip_power_cycle();

    /* DIAGNOSTIC: send a wake-up write FIRST in each encoding, then
     * read TEST_RO with the same encoding. The chip's TEST_RO might
     * be gated behind WAKE_UP — without it the chip returns its
     * canned status byte (we saw 0x06/0x60 repeating) instead of
     * the canonical 0xFEEDBEAD pattern. The wake byte 0xB3 must
     * land in byte 0 of BUS_CTRL on the chip side. */
    {
        uint32_t wake_cmd = gspi_cmd_word(1U, GSPI_FUNCTION_BUS, 1U,
                                          GSPI_REG_SPI_BUS_CTRL, 4U);
        uint32_t wake_val = GSPI_BUS_CTRL_PHASE1_WAKE;
        /* Pre-wake using bswap32 encoding (= chip in 16-bit-LE at
         * boot puts byte 0 of value at wire byte 0 → register byte
         * 0 of BUS_CTRL, where WAKE_UP / WORD_LENGTH_32 / ENDIAN
         * live). After this write the chip should flip to 32-bit
         * BE for subsequent transactions. */
        rc = gspi_xfer(gspi_bswap32(wake_cmd),
                       &(uint32_t){ gspi_bswap32(wake_val) }, 1U,
                       (uint32_t *)0, 0U);
        CYW43_PRINTF("probe: pre-wake bswap32 write val=0x%lx (rc=%d)\n",
                     (unsigned long)wake_val, rc);
        tiku_common_delay_ms(20U);
    }

    boot_cmd = gspi_cmd_word(0U, GSPI_FUNCTION_BUS, 1U,
                             GSPI_REG_SPI_TEST_RO, 4U);
    for (i = 0U; i < sizeof(boot_encodings) / sizeof(boot_encodings[0]); ++i) {
        uint32_t encoded_cmd = boot_encodings[i].encode != 0
                             ? boot_encodings[i].encode(boot_cmd)
                             : boot_cmd;
        raw = 0UL;
        rc = gspi_read32_encoded(encoded_cmd, &raw);
        CYW43_PRINTF("probe: boot TEST_RO %s cmd=0x%lx raw=0x%lx "
                     "rot=0x%lx rev16=0x%lx bswap=0x%lx (rc=%d)\n",
                     boot_encodings[i].name,
                     (unsigned long)encoded_cmd,
                     (unsigned long)raw,
                     (unsigned long)gspi_swap16(raw),
                     (unsigned long)gspi_rev16x2(raw),
                     (unsigned long)gspi_bswap32(raw),
                     rc);
        if (rc != TIKU_DRV_OK) {
            return rc;
        }
        v = boot_encodings[i].decode != 0
          ? boot_encodings[i].decode(raw)
          : raw;
        if (v == GSPI_TEST_RO_PATTERN) {
            break;
        }
        tiku_common_delay_ms(2U);
    }
    if (v != GSPI_TEST_RO_PATTERN) {
        CYW43_PRINTF("probe: no boot TEST_RO encoding reached 0x%08lx\n",
                     (unsigned long)GSPI_TEST_RO_PATTERN);
        return TIKU_DRV_ERR_NOT_PRESENT;
    }

    rc = gspi_write32_swapped(GSPI_FUNCTION_BUS,
                              GSPI_REG_SPI_TEST_RW,
                              GSPI_TEST_RW_PATTERN);
    CYW43_PRINTF("probe: boot TEST_RW write 0x%08lx (rc=%d)\n",
                 (unsigned long)GSPI_TEST_RW_PATTERN, rc);
    if (rc != TIKU_DRV_OK) {
        return rc;
    }

    rc = gspi_read32_swapped(GSPI_FUNCTION_BUS,
                             GSPI_REG_SPI_TEST_RW, &v);
    CYW43_PRINTF("probe: boot TEST_RW read = 0x%08lx (rc=%d) %s\n",
                 (unsigned long)v, rc,
                 (v == GSPI_TEST_RW_PATTERN) ? "*** MATCH ***" : "mismatch");
    if (rc != TIKU_DRV_OK) {
        return rc;
    }
    if (v != GSPI_TEST_RW_PATTERN) {
        return TIKU_DRV_ERR_NOT_PRESENT;
    }

    /* Phase 1 OK — both gates passed. */
    CYW43_PRINTF("probe: phase 1 OK (TEST_RO=0xfeedbead, "
                 "TEST_RW round-trip MATCH)\n");

    /*-----------------------------------------------------------*/
    /* PHASE 2.A: full chip configure write (single-step).       */
    /*-----------------------------------------------------------*/
    /* Per embassy-rs cyw43 reference (the working Apache-2.0
     * driver): single write of WORD_LENGTH_32 | HIGH_SPEED |
     * INT_POL_HIGH | WAKE_UP at byte 0, RESP_DELAY=4 at byte 1,
     * STATUS_ENABLE | INTR_WITH_STATUS at byte 2. CRITICAL: NO
     * ENDIAN_BIG — chip's default endianness is what we want.
     * Setting ENDIAN_BIG (0xB3 instead of 0xB1) puts the chip in
     * a state where every register read echoes the wake byte
     * (the 0xB3B3B3B3 stuck pattern we hit before). */
    rc = gspi_write32_swapped(GSPI_FUNCTION_BUS,
                              GSPI_REG_SPI_BUS_CTRL,
                              GSPI_BUS_CTRL_PHASE2_WAKE);
    CYW43_PRINTF("p2.A: wake/configure write 0x%lx (rc=%d)\n",
                 (unsigned long)GSPI_BUS_CTRL_PHASE2_WAKE, rc);
    if (rc != TIKU_DRV_OK) return rc;

    /* Chip is now in 32-bit-word BE wire mode. Flip the API so
     * subsequent cyw43_gspi_read32/write32 use no swap. */
    gspi.configured_32 = 1U;

    /* Verify: read TEST_RO via no-swap path. Should be 0xFEEDBEAD. */
    rc = cyw43_gspi_read32(GSPI_FUNCTION_BUS,
                           GSPI_REG_SPI_TEST_RO, &v);
    if (rc != TIKU_DRV_OK || v != GSPI_TEST_RO_PATTERN) {
        CYW43_PRINTF("p2.A: post-config TEST_RO=0x%08lx FAIL "
                     "(rc=%d, expected 0xfeedbead)\n",
                     (unsigned long)v, rc);
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    CYW43_PRINTF("p2.A: post-config TEST_RO=0xfeedbead "
                 "*** phase 2.A OK ***\n");

    /* Set SPI_RESP_DELAY_F1 = 4. Chip stores register-byte-0 at
     * value LSB byte (LE memory layout), so 0x04 in LSB position. */
    rc = cyw43_gspi_write32(GSPI_FUNCTION_BUS,
                            GSPI_REG_SPI_RESP_DELAY_F1,
                            (uint32_t)GSPI_BACKPLANE_READ_PADD_BYTES);
    CYW43_PRINTF("p2.A: SPI_RESP_DELAY_F1 write (rc=%d)\n", rc);

    /* Request ALP clock at F1/0x1000E byte 0 = 0x08
     * (BACKPLANE_ALP_AVAIL_REQ). Value LSB byte at register byte 0. */
    rc = cyw43_gspi_write32(GSPI_FUNCTION_BACKPLANE,
                            0x1000EUL,
                            0x00000008UL);
    CYW43_PRINTF("p2.A: ALP request F1/0x1000E (rc=%d)\n", rc);

    /* Poll CSR until ALP_AVAIL (= 0x40 in register byte 0 = value LSB). */
    {
        unsigned int attempt;
        for (attempt = 0U; attempt < 50U; ++attempt) {
            uint32_t cmd = gspi_cmd_word(0U, GSPI_FUNCTION_BACKPLANE,
                                         1U, 0x1000EUL, 4U);
            uint32_t buf[2] = { 0UL, 0UL };
            tiku_common_delay_ms(2U);
            (void)gspi_xfer(cmd, (const uint32_t *)0, 0U, buf, 2U);
            v = buf[1];
            if ((v & 0xFFU) & 0x40U) {
                CYW43_PRINTF("p2.A: ALP_AVAIL after %u polls "
                             "(CSR=0x%08lx)\n",
                             attempt, (unsigned long)v);
                break;
            }
            if (attempt < 3U || attempt == 49U) {
                CYW43_PRINTF("p2.A: ALP poll[%u] CSR=0x%08lx "
                             "delay[0x%lx]\n",
                             attempt, (unsigned long)v,
                             (unsigned long)buf[0]);
            }
        }
    }

    rc = cyw43_gspi_write32(GSPI_FUNCTION_BACKPLANE,
                            0x1000EUL, 0x00000000UL);
    CYW43_PRINTF("p2.A: ALP request cleared (rc=%d)\n", rc);

    /*-----------------------------------------------------------*/
    /* PHASE 2.B: F1 backplane chipcommon ChipID = 0xA9A6.        */
    /*-----------------------------------------------------------*/
    /* Write SBADDR window for chipcommon (backplane 0x18000000).
     * Chip's LE memory storage: register-byte-0 at value LSB,
     * register-byte-3 at value MSB. So for SBADDR_HIGH (0x1000C =
     * register-byte-2 of the 32-bit write at 0x1000A) we want
     * value bits 23:16 = 0x18 → value = 0x00180000. */
    rc = cyw43_gspi_write32(GSPI_FUNCTION_BACKPLANE,
                            GSPI_REG_SBADDR_LOW,
                            0x00180000UL);
    CYW43_PRINTF("p2.B: SBADDR write 0x00180000 -> 0x18000000 "
                 "(rc=%d)\n", rc);

    /* Read chipcommon ChipID at backplane offset 0 within the
     * 0x18000000 window. bus_addr = (0 & 0x7FFF) | 0x8000 (32-bit
     * access flag) = 0x8000. F1 backplane reads transfer TWO
     * 32-bit words: first is response-delay padding (discard),
     * second is actual register data. */
    {
        uint32_t cmd = gspi_cmd_word(0U, GSPI_FUNCTION_BACKPLANE,
                                     1U, 0x8000UL, 4U);
        uint32_t buf[2] = { 0UL, 0UL };
        rc = gspi_xfer(cmd, (const uint32_t *)0, 0U, buf, 2U);
        CYW43_PRINTF("p2.B: F1/0x8000 chipcommon raw[0]=0x%08lx "
                     "raw[1]=0x%08lx (rc=%d)\n",
                     (unsigned long)buf[0], (unsigned long)buf[1], rc);
        v = buf[1];
    }

    if ((v & 0xFFFFUL) == GSPI_CHIP_ID_CYW43439) {
        CYW43_PRINTF("p2.B: *** chipcommon ChipID=0x%08lx (CYW43439) "
                     "— phase 2.B OK ***\n", (unsigned long)v);
    } else {
        CYW43_PRINTF("p2.B: chipcommon ChipID=0x%08lx mismatch "
                     "(expected low 16 bits = 0x%lx)\n",
                     (unsigned long)v,
                     (unsigned long)GSPI_CHIP_ID_CYW43439);
        return TIKU_DRV_ERR_NOT_PRESENT;
    }

    /*-----------------------------------------------------------*/
    /* PHASE 2.C: disable WLAN (ARM) + SOCSRAM cores, init SOCSRAM. */
    /*-----------------------------------------------------------*/
    /* Sequence mirrors embassy-rs cyw43 runner::init() up to the
     * point just before firmware upload:
     *   1. disable ARM core with CPUHALT
     *   2. disable SOCSRAM (no halt — it's not a CPU core)
     *   3. reset SOCSRAM (brings clock back, takes it out of reset)
     *   4. chip-specific SOCSRAM init for C43439:
     *        write 3 to SOCSRAM_BASE + 0x10 (disable SRAM_3 remap)
     *        write 0 to SOCSRAM_BASE + 0x44
     */
    rc = gspi_disable_core(GSPI_BACKPLANE_ARM_CORE_BASE, 1U, "arm");
    if (rc != TIKU_DRV_OK) return rc;

    rc = gspi_disable_core(GSPI_BACKPLANE_SOCSRAM_WRAP, 0U, "socsram");
    if (rc != TIKU_DRV_OK) return rc;

    rc = gspi_reset_core(GSPI_BACKPLANE_SOCSRAM_WRAP, 0U, "socsram");
    if (rc != TIKU_DRV_OK) return rc;

    rc = cyw43_gspi_bp_write32(GSPI_BACKPLANE_SOCSRAM_BASE + 0x10UL, 3UL);
    if (rc != TIKU_DRV_OK) {
        CYW43_PRINTF("p2.C: SOCSRAM bankxinfo write FAIL rc=%d\n", rc);
        return rc;
    }
    rc = cyw43_gspi_bp_write32(GSPI_BACKPLANE_SOCSRAM_BASE + 0x44UL, 0UL);
    if (rc != TIKU_DRV_OK) {
        CYW43_PRINTF("p2.C: SOCSRAM bankpda write FAIL rc=%d\n", rc);
        return rc;
    }

    /* Confirm SOCSRAM came back up. */
    rc = gspi_core_is_up(GSPI_BACKPLANE_SOCSRAM_WRAP, "socsram");
    if (rc != TIKU_DRV_OK) {
        CYW43_PRINTF("p2.C: SOCSRAM did not come up after reset\n");
        return rc;
    }

    CYW43_PRINTF("p2.C: *** cores disabled, SOCSRAM init OK "
                 "— phase 2.C done ***\n");

    /*-----------------------------------------------------------*/
    /* PHASE 2.F: upload firmware + NVRAM, release ARM core,     */
    /*            wait for HT clock to confirm firmware running. */
    /*-----------------------------------------------------------*/
    {
        uint32_t fw_size       = cyw43_firmware_size;
        uint32_t nvram_size    = cyw43_nvram_size;
        uint32_t nvram_padded  = (nvram_size + 3UL) & ~3UL;
        uint32_t nvram_addr    = GSPI_CHIP_RAM_BASE + GSPI_CHIP_RAM_SIZE
                                 - 4UL - nvram_padded;
        uint32_t magic_addr    = GSPI_CHIP_RAM_BASE + GSPI_CHIP_RAM_SIZE
                                 - 4UL;
        uint32_t nvram_words   = nvram_padded / 4UL;
        uint32_t magic_word    = ((~nvram_words) << 16) | nvram_words;
        uint32_t magic_rb      = 0UL;
        uint8_t  fw_check[16];
        uint32_t i;
        uint8_t  csr_val       = 0U;
        unsigned int htries;

        CYW43_PRINTF("p2.F: uploading firmware (%lu bytes) to chip RAM\n",
                     (unsigned long)fw_size);
        rc = cyw43_gspi_bp_write(GSPI_CHIP_RAM_BASE,
                                 cyw43_firmware_data, fw_size);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p2.F: firmware bp_write FAIL rc=%d\n", rc);
            return rc;
        }
        CYW43_PRINTF("p2.F: firmware upload done\n");

        /* Spot-verify: read back the first 16 bytes of firmware and
         * compare. A bad block_xfer would corrupt this byte-for-byte
         * (versus a more subtle bug that only shows up later). The
         * upload's last chunk left SBADDR pointing at some high
         * window; bp_set_window reseats it on chip-RAM base 0 before
         * the read_block walks bytes 0..15. */
        rc = bp_set_window(GSPI_CHIP_RAM_BASE);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p2.F: fw spot-read SBADDR FAIL rc=%d\n", rc);
            return rc;
        }
        rc = cyw43_gspi_read_block(GSPI_FUNCTION_BACKPLANE,
                                   GSPI_CHIP_RAM_BASE
                                       & GSPI_BACKPLANE_ADDRESS_MASK,
                                   fw_check, sizeof fw_check);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p2.F: fw spot-read FAIL rc=%d\n", rc);
            return rc;
        }
        {
            int match = 1;
            for (i = 0U; i < sizeof fw_check; ++i) {
                if (fw_check[i] != cyw43_firmware_data[i]) {
                    match = 0;
                    break;
                }
            }
            if (!match) {
                CYW43_PRINTF("p2.F: fw spot-verify MISMATCH at byte %lu "
                             "(got 0x%02x, expected 0x%02x)\n",
                             (unsigned long)i,
                             fw_check[i], cyw43_firmware_data[i]);
                return TIKU_DRV_ERR_NOT_PRESENT;
            }
            CYW43_PRINTF("p2.F: fw spot-verify OK (first 16 bytes match)\n");
        }

        CYW43_PRINTF("p2.F: uploading NVRAM (%lu bytes -> 0x%08lx)\n",
                     (unsigned long)nvram_size,
                     (unsigned long)nvram_addr);
        rc = cyw43_gspi_bp_write(nvram_addr, cyw43_nvram_data, nvram_size);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p2.F: nvram bp_write FAIL rc=%d\n", rc);
            return rc;
        }

        /* Verify NVRAM first 4 bytes BEFORE the ARM reset — once the
         * firmware starts running, it parses NVRAM in-place and then
         * repurposes that region as runtime scratch, so the readback
         * value only matches what we wrote during this window. The
         * NVRAM text always starts with "NVRAMRev", so byte 0..3
         * spell "NVRA" → host-LE u32 = 0x4152564E. */
        {
            uint32_t nv_first   = 0UL;
            uint32_t expected   = (uint32_t)cyw43_nvram_data[0]        |
                                  ((uint32_t)cyw43_nvram_data[1] <<  8) |
                                  ((uint32_t)cyw43_nvram_data[2] << 16) |
                                  ((uint32_t)cyw43_nvram_data[3] << 24);
            rc = cyw43_gspi_bp_read32(nvram_addr, &nv_first);
            if (rc != TIKU_DRV_OK || nv_first != expected) {
                CYW43_PRINTF("p2.F: nvram first-word verify FAIL "
                             "(got 0x%08lx, expected 0x%08lx, rc=%d)\n",
                             (unsigned long)nv_first,
                             (unsigned long)expected, rc);
                return TIKU_DRV_ERR_NOT_PRESENT;
            }
            CYW43_PRINTF("p2.F: nvram first-word 0x%08lx OK "
                         "(spells 'NVRA')\n", (unsigned long)nv_first);
        }

        rc = cyw43_gspi_bp_write32(magic_addr, magic_word);
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p2.F: nvram magic write FAIL rc=%d\n", rc);
            return rc;
        }
        rc = cyw43_gspi_bp_read32(magic_addr, &magic_rb);
        if (rc != TIKU_DRV_OK || magic_rb != magic_word) {
            CYW43_PRINTF("p2.F: nvram magic readback FAIL "
                         "(got 0x%08lx, expected 0x%08lx, rc=%d)\n",
                         (unsigned long)magic_rb,
                         (unsigned long)magic_word, rc);
            return TIKU_DRV_ERR_NOT_PRESENT;
        }
        CYW43_PRINTF("p2.F: nvram magic 0x%08lx @ 0x%08lx OK\n",
                     (unsigned long)magic_word,
                     (unsigned long)magic_addr);

        /* Bring the WLAN/ARM core out of reset. halt=false so the
         * ARM starts executing the firmware we just uploaded. */
        rc = gspi_reset_core(GSPI_BACKPLANE_ARM_CORE_BASE, 0U, "arm");
        if (rc != TIKU_DRV_OK) return rc;

        rc = gspi_core_is_up(GSPI_BACKPLANE_ARM_CORE_BASE, "arm");
        if (rc != TIKU_DRV_OK) {
            CYW43_PRINTF("p2.F: ARM core did not come up after reset\n");
            return rc;
        }

        /* Poll for HT clock — set by the firmware once it has booted
         * far enough to switch from ALP to HT and signal readiness.
         * Embassy uses 500 ms; we poll every 2 ms for 250 attempts.
         * CHIP_CLOCK_CSR is an F1-direct register at address 0x1000E
         * (NOT a backplane window offset), so use the function-1
         * read8 path rather than bp_read8. */
        for (htries = 0U; htries < 250U; ++htries) {
            tiku_common_delay_ms(2U);
            rc = cyw43_gspi_read8(GSPI_FUNCTION_BACKPLANE,
                                  GSPI_REG_CHIP_CLOCK_CSR, &csr_val);
            if (rc != TIKU_DRV_OK) continue;
            if (csr_val & GSPI_CHIP_CLOCK_HT_AVAIL) {
                CYW43_PRINTF("p2.F: HT clock UP after %u polls "
                             "(CSR=0x%02x) *** firmware running ***\n",
                             htries, csr_val);
                break;
            }
        }
        if (!(csr_val & GSPI_CHIP_CLOCK_HT_AVAIL)) {
            CYW43_PRINTF("p2.F: HT clock TIMEOUT after %u polls "
                         "(last CSR=0x%02x) — firmware did not boot\n",
                         htries, csr_val);
            return TIKU_DRV_ERR_TIMEOUT;
        }

        CYW43_PRINTF("p2.F: *** firmware boot OK — phase 2.F done ***\n");

        /*-------------------------------------------------------*/
        /* POST-BOOT STATUS PROBE (diagnostic baseline for       */
        /* phase 3). Three reads — NVRAM verify moved to the     */
        /* pre-ARM-reset window above since the firmware         */
        /* repurposes that region as runtime scratch:            */
        /*   1. SPI_STATUS         — F2 readiness + pkt length    */
        /*   2. SPI_INT_REG        — pending IRQ bits, clear      */
        /*   3. Chipcommon ChipID  — backplane bus still alive    */
        /*-------------------------------------------------------*/
        {
            uint32_t status = 0UL;
            uint32_t intreg = 0UL;
            uint32_t cc_id  = 0UL;

            /* 1. SPI_STATUS — bits 9..19 hold F2 packet length;
             *    bit 5 = F2_RX_READY (data path up). F2_RX_READY
             *    stays low until phase 3's F2 watermark write at
             *    F1/REG_BACKPLANE_FUNCTION2_WATERMARK = 0x10008. */
            rc = cyw43_gspi_read32(GSPI_FUNCTION_BUS,
                                   GSPI_REG_SPI_STATUS, &status);
            CYW43_PRINTF("post: SPI_STATUS = 0x%08lx (rc=%d) "
                         "F2_RX_READY=%lu F2_PKT_AVAIL=%lu "
                         "F2_PKT_LEN=%lu\n",
                         (unsigned long)status, rc,
                         (unsigned long)((status & GSPI_STATUS_F2_RX_READY)
                                          ? 1U : 0U),
                         (unsigned long)((status &
                                          GSPI_STATUS_F2_PKT_AVAILABLE)
                                          ? 1U : 0U),
                         (unsigned long)((status &
                                          GSPI_STATUS_F2_PKT_LEN_MASK)
                                          >> GSPI_STATUS_F2_PKT_LEN_SHIFT));

            /* 2. SPI_INT_REG — write-1-to-clear. Read first to
             *    snapshot pending bits, then clear them. */
            rc = cyw43_gspi_read32(GSPI_FUNCTION_BUS,
                                   GSPI_REG_SPI_INT_REG, &intreg);
            CYW43_PRINTF("post: SPI_INT_REG = 0x%04lx (rc=%d)\n",
                         (unsigned long)(intreg & 0xFFFFUL), rc);
            if (rc == TIKU_DRV_OK && (intreg & 0xFFFFUL) != 0UL) {
                (void)cyw43_gspi_write32(GSPI_FUNCTION_BUS,
                                         GSPI_REG_SPI_INT_REG,
                                         intreg & 0xFFFFUL);
                CYW43_PRINTF("post: SPI_INT_REG cleared\n");
            }

            /* 3. Chipcommon ChipID re-read. SBADDR walks back to
             *    0x18000000 via bp_set_window. Expected value =
             *    same 0x1545A9AF we saw in 2.B (chip ID = 0xA9AF
             *    in low 16 bits). */
            rc = cyw43_gspi_bp_read32(GSPI_BACKPLANE_CHIPCOMMON_BASE,
                                      &cc_id);
            CYW43_PRINTF("post: chipcommon ChipID = 0x%08lx (rc=%d) "
                         "chipid=0x%04lx rev=0x%lx pkg=0x%lx %s\n",
                         (unsigned long)cc_id, rc,
                         (unsigned long)(cc_id & 0xFFFFUL),
                         (unsigned long)((cc_id >> 16) & 0xFFFUL),
                         (unsigned long)((cc_id >> 28) & 0xFUL),
                         ((cc_id & 0xFFFFUL) == GSPI_CHIP_ID_CYW43439)
                             ? "OK" : "BACKPLANE BROKEN");
        }
    }

    gspi.ready = 1U;
    return TIKU_DRV_OK;
}

/*---------------------------------------------------------------------------*/
/* STATUS-REGISTER POLL                                                      */
/*---------------------------------------------------------------------------*/

/*
 * Bounded poll on the gSPI status register. Used by phase-2+
 * code (firmware upload, F2 ready handshake) that needs to wait
 * for the chip to flip a specific status bit. Argument shape:
 *
 *     wait_status(mask, expected):
 *       while ((status & mask) != expected) ...
 *
 * Returns OK as soon as the masked compare matches, or
 * ERR_TIMEOUT after CYW43_GSPI_STATUS_LIMIT retries
 * (each separated by ~100 µs).
 */
int cyw43_gspi_wait_status(uint32_t mask, uint32_t expected)
{
    uint32_t status;
    uint32_t tries;
    int      rc;

    for (tries = 0U; tries < CYW43_GSPI_STATUS_LIMIT; ++tries) {
        rc = cyw43_gspi_read32(GSPI_FUNCTION_BUS,
                               GSPI_REG_SPI_STATUS, &status);
        if (rc != TIKU_DRV_OK) {
            return rc;
        }
        if ((status & mask) == expected) {
            return TIKU_DRV_OK;
        }
        tiku_common_delay_us(100U);
    }
    return TIKU_DRV_ERR_TIMEOUT;
}
