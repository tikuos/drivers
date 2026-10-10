/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * ieee154_c5.c - IEEE 802.15.4 PHY on the C5's 15.4 MAC block, polled.
 * Register fields follow ESP-IDF 4d59230: ieee802154 driver and common_ll,
 * esp32c5 ieee802154_reg.h, modem_syscon_struct.h and modem_clock_impl.c.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_ieee154_hal.h>
#include "tiku_drv_phy_c5.h"

/* Beside BLE (TIKU_C5_154_COEX) the MAC is a radio of the adapter in
 * ../esp_core.c: the PHY, the clocks and the baseband come from it, and
 * the arbiter in libcoexist.a sets the MAC's priorities from the events of
 * esp_coex_i154.h through the hooks below. */
#ifndef TIKU_C5_154_COEX
#define TIKU_C5_154_COEX 0
#endif
#if TIKU_C5_154_COEX
#include "../esp_coex_abi.h"
#include "../esp_port.h"
#define COEX_MIDDLE 2
#define COEX_LOW    3
#define COEX_IDLE   4
void esp_coex_ieee802154_txrx_pti_set(int event);
void esp_coex_ieee802154_ack_pti_set(int event);
void esp_coex_ieee802154_coex_break_notify(void);
void esp_coex_ieee802154_status_enable(void);
void esp_coex_ieee802154_status_disable(void);
#endif
#include <stdbool.h>
#include <string.h>
#include <arch/esp32c5/tiku_esp32c5_regs.h>
#include <arch/esp32c5/tiku_irq_arch.h>
#include <arch/esp32c5/tiku_cpu_common.h>
#include <hal/tiku_printf_hal.h>
#include <kernel/cpu/tiku_watchdog.h>
#include "tiku.h"                   /* the platform's tick, for listen windows */
#include <tikukits/crypto/ccm/tiku_kits_crypto_ccm.h>

/* Vendor entry points: PLL tracking for the BLE/15.4 side (libphy), the
 * BLE/15.4 baseband, the MAC's TX/RX ramp delays, the last RSSI and the TX
 * power table (libbtbb). */
extern void phy_param_track_tot(bool en_wifi, bool en_ble_154);
extern void bt_bb_v2_init_cmplx(uint8_t print_version);
extern void ieee802154_txon_delay_set(void);
extern uint32_t bt_bb_get_cur_rx_info(void);
extern const int8_t *bt_bb_get_tx_pwr_table(uint8_t *length);

/* The 15.4 clock domain (IEEE802154_CLOCK_DEPS): in MODEM_SYSCON CLK_CONF the
 * ETM, 15.4 APB, 15.4 MAC and modem-security APB clocks; in CLK_CONF1 the BT
 * APB and the baseband shared with BLE.  The domain's gate map (CLK_CONF_
 * POWER_ST bits 11:8) opens it in the active ICG code, 2. */
#define SYSCON_CLK    0x600A9C04u
#define SYSCON_MAP    0x600A9C0Cu
#define SYSCON_RESET  0x600A9C10u
#define SYSCON_CLK1   0x600A9C14u
#define ZB_CLOCKS     ((1u << 22) | (1u << 23) | (1u << 24) | (1u << 28))
#define ZB_CLOCKS1    ((1u << 16) | (1u << 17))
#define ZB_MAP        (1u << (8u + 2u))
#define ZB_RESET      ((1u << 23) | (1u << 24))

/* The 15.4 MAC block. */
#define ZB_BASE        0x600A3000u
#define ZB_CMD         (ZB_BASE + 0x000u)
#define ZB_CTRL        (ZB_BASE + 0x004u)
#define ZB_INF0_ADDR   (ZB_BASE + 0x008u)
#define ZB_INF0_PAN    (ZB_BASE + 0x00Cu)
#define ZB_CHANNEL     (ZB_BASE + 0x048u)
#define ZB_TXPOWER     (ZB_BASE + 0x04Cu)
#define ZB_ED_DURATION (ZB_BASE + 0x050u)
#define ZB_ED_CFG      (ZB_BASE + 0x054u)
#define ZB_EVENT_EN    (ZB_BASE + 0x060u)
#define ZB_EVENT       (ZB_BASE + 0x064u)
#define ZB_RX_ABORT_EN (ZB_BASE + 0x068u)
#define ZB_PTI         (ZB_BASE + 0x070u)
#define ZB_RX_STATUS   (ZB_BASE + 0x080u)
#define ZB_TX_STATUS   (ZB_BASE + 0x084u)
#define ZB_TXDMA_ADDR  (ZB_BASE + 0x0D0u)
#define ZB_RXDMA_ADDR  (ZB_BASE + 0x0E0u)
#define ZB_DATE        (ZB_BASE + 0x184u)
#define CMD_TX_START   0x41u
#define CMD_RX_START   0x42u
#define CMD_ED_START   0x44u
#define CMD_STOP       0x45u
#define EV_TX_DONE     (1u << 0)
#define EV_RX_DONE     (1u << 1)
#define EV_ACK_TX_DONE (1u << 2)
#define EV_RX_ABORT    (1u << 4)
#define EV_TX_ABORT    (1u << 5)
#define EV_ED_DONE     (1u << 6)
#define EV_ALL         (EV_TX_DONE | EV_RX_DONE | EV_ACK_TX_DONE | \
                        EV_RX_ABORT | EV_TX_ABORT | EV_ED_DONE)
/* CTRL_CFG: send ACKs for received frames, wait for an ACK after sending,
 * accept frames for any address, filter on interface 0's PAN and address. */
#define CTRL_ACK_TX    (1u << 0)
#define CTRL_ACK_RX    (1u << 3)
#define CTRL_PROMISC   (1u << 7)
#define CTRL_INF0      (1u << 28)
/* ED_CFG: CCA threshold in dBm (bits 7:0), maximum or average over the
 * window (bit 13, clear: maximum), CCA by energy (bits 15:14 = 1), the
 * measured level (bits 23:16) and the CCA verdict (bit 24). */
#define ED_SAMPLE_AVG  (1u << 13)
#define CCA_MODE_MASK  (3u << 14)
#define CCA_MODE_ED    (1u << 14)
#define CCA_BUSY       (1u << 24)
/* RX_STATUS bits 8:4 hold the abort reason: 3 a frame with a bad FCS, 16 an
 * ACK the MAC block could not send in time, 18 one coexistence took away.
 * The abort event fires for the reasons enabled one bit below.  TX_STATUS
 * bits 8:4 hold a transmit abort's reason, 18 again the arbiter's. */
#define ABORT_CRC      3u
#define ABORT_COEX     18u
#define ABORT_EVENTS   ((1u << (3u - 1u)) | (1u << (16u - 1u)) | \
                        (1u << (18u - 1u)))

/* ED window in 16 us symbols: 64 is 1.02 ms, about the nRF54L's 8 x 128 us;
 * a CCA listens for the standard 8 symbols. */
#define ED_SYMBOLS     64u
#define CCA_SYMBOLS    8u
#define CCA_DBM        (-75)
#define WAIT_SPINS     2000000u
/* The ACK follows a frame within 192 us and lasts 352 us.  After sending it
 * the MAC block turns back to receive (TX/RX switch 122 us, RX on 50 us); a
 * stop inside that turnaround leaves its next ACK to time out. */
#define ACK_SPINS      200000u
#define ACK_SETTLE_US  300u
/* After a stop the RF chain needs a few microseconds to wind down.  Closing
 * the PHY inside that window leaves it unable to measure or receive on every
 * later bring-up until the chip is reset: with no pause the third bring-up
 * failed, with 5 us none did. */
#define STOP_SETTLE_US 50u
/* The nRF54L reports ED as dBm + 94; the C5's level uses the same scale. */
#define ED_FLOOR_DBM   (-94)
/* Transmit power: the baseband table entry nearest 0 dBm from below, the
 * nRF54L port's power. */
#define TX_DBM         0

#ifndef TIKU_C5_154_TRACE
#define TIKU_C5_154_TRACE 0
#endif

/* The MAC's DMA reads [PHR][frame without FCS] to send, the PHR counting the
 * FCS it appends; it writes [PHR][frame][RSSI][LQI] on receive, the RSSI and
 * LQI in the FCS's place. */
static uint8_t tx_frame[1u + TIKU_154_MAX_FRAME] __attribute__((aligned(4)));
static uint8_t rx_frame[1u + TIKU_154_MAX_FRAME] __attribute__((aligned(4)));
#if !TIKU_C5_154_COEX
static uint32_t saved_clk, saved_clk1, saved_map;
#endif
static uint8_t active;
static uint8_t cur_chan = TIKU_154_CHAN_MIN;

/** @brief Set the bits of @p mask in @p address to @p value. */
static void field(uint32_t address, uint32_t mask, uint32_t value)
{
    TIKU_C5_REG_WRITE(address,
                      (TIKU_C5_REG_READ(address) & ~mask) | (value & mask));
}

/** @brief Clamp @p channel to 11..26. */
static uint8_t clamp(uint8_t channel)
{
    if (channel < TIKU_154_CHAN_MIN) {
        return TIKU_154_CHAN_MIN;
    }
    return channel > TIKU_154_CHAN_MAX ? TIKU_154_CHAN_MAX : channel;
}

/** @brief Stop whatever the MAC runs and clear the events it left. */
static void mac_stop(void)
{
    TIKU_C5_REG_WRITE(ZB_CMD, CMD_STOP);
    TIKU_C5_REG_WRITE(ZB_EVENT, EV_ALL);
}

/**
 * @brief Poll for any of @p events for at most @p spins reads.
 * @return the events seen, or 0
 */
static uint32_t wait_events(uint32_t events, uint32_t spins)
{
    uint32_t spin, seen;
    for (spin = 1; spin <= spins; spin++) {
        seen = TIKU_C5_REG_READ(ZB_EVENT) & events;
        if (seen) {
            return seen;
        }
        if ((spin & 0xFFFFu) == 0u) {
            tiku_watchdog_kick();
        }
    }
    return 0;
}

#if !TIKU_C5_154_COEX
/** @brief Open the 15.4 clocks and gate map; the PHY is already on. */
static void clocks_on(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    saved_clk = TIKU_C5_REG_READ(SYSCON_CLK);
    saved_clk1 = TIKU_C5_REG_READ(SYSCON_CLK1);
    saved_map = TIKU_C5_REG_READ(SYSCON_MAP);
    field(SYSCON_CLK, ZB_CLOCKS, ZB_CLOCKS);
    field(SYSCON_CLK1, ZB_CLOCKS1, ZB_CLOCKS1);
    field(SYSCON_MAP, ZB_MAP, ZB_MAP);
    TIKU_C5_IRQ_RESTORE(state);
}

/** @brief Return the 15.4 clock and map fields to what clocks_on found. */
static void clocks_off(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    field(SYSCON_MAP, ZB_MAP, saved_map);
    field(SYSCON_CLK1, ZB_CLOCKS1, saved_clk1);
    field(SYSCON_CLK, ZB_CLOCKS, saved_clk);
    TIKU_C5_IRQ_RESTORE(state);
}
#endif

/**
 * @brief The PHY, the 15.4 clocks and the baseband on: alone from the PHY
 *        driver, beside BLE from the adapter both MACs share, with the
 *        arbiter initialised and enabled as its other users do.
 * @return 0, or -1 when the PHY cannot be taken
 */
static int radio_on(void)
{
#if TIKU_C5_154_COEX
    if (espw_core_up(ESPW_RADIO_154, 0u, 0u) != 0) {
        return -1;
    }
    espw_modem_154_on();
    espw_phy_154_enable();
    espw_btbb_enable();
    (void)coex_init();
    (void)coex_enable();
#else
    if (tiku_drv_phy_c5_on() != TIKU_C5_PHY_OK) {
        return -1;
    }
    clocks_on();
    phy_param_track_tot(false, true);
    bt_bb_v2_init_cmplx(0);
#endif
    return 0;
}

/** @brief The reverse of radio_on(); the PHY closes before the clocks. */
static void radio_off(void)
{
#if TIKU_C5_154_COEX
    espw_btbb_disable();
    espw_phy_154_disable();
    espw_modem_154_off();
    espw_core_down(ESPW_RADIO_154);
#else
    (void)tiku_drv_phy_c5_off();
    clocks_off();
#endif
}

/** @brief Tell the arbiter the MAC is about to send or receive, or idle. */
static void scene(int busy)
{
#if TIKU_C5_154_COEX
    esp_coex_ieee802154_txrx_pti_set(busy ? COEX_LOW : COEX_IDLE);
#else
    (void)busy;
#endif
}

/** @brief The baseband power table's index for TX_DBM, as ESP-IDF picks it. */
static uint32_t power_index(void)
{
    uint8_t length = 0, i;
    const int8_t *table = bt_bb_get_tx_pwr_table(&length);
    if (!table || !length) {
        return 0;
    }
    for (i = (uint8_t)(length - 1u); i != 0u; i--) {
        if (table[i] <= TX_DBM) {
            break;
        }
    }
#if TIKU_C5_154_TRACE
    {
        uint8_t k;
        TIKU_PRINTF("154 C5: power table");
        for (k = 0; k < length; k++) {
            TIKU_PRINTF(" %d", (int)table[k]);
        }
        TIKU_PRINTF(" -> index %u\n", (unsigned)i);
    }
#endif
    return i;
}

int tiku_ieee154_arch_available(void)
{
    return 1;
}

/* Alone, the profile holds the PHY the SDR and the radios would share, and
 * tiku_drv_phy_c5_on() refuses one another user holds; beside BLE the
 * adapter shares it. */
int tiku_ieee154_arch_mode_154(uint8_t channel)
{
    if (!active) {
        if (radio_on() != 0) {
            return -1;
        }
        /* The MAC reset, then the configuration ESP-IDF's mac_init gives it:
         * ramp delays, the arbiter's priorities (ACKs middle, frames low,
         * idle otherwise) or arbitration off (PTI 3) alone, events, CCA by
         * energy at -75 dBm, the transmit power. */
        field(SYSCON_RESET, ZB_RESET, ZB_RESET);
        field(SYSCON_RESET, ZB_RESET, 0);
        ieee802154_txon_delay_set();
#if TIKU_C5_154_COEX
        esp_coex_ieee802154_ack_pti_set(COEX_MIDDLE);
        scene(0);
#else
        TIKU_C5_REG_WRITE(ZB_PTI, (3u << 4) | 3u);
#endif
        field(ZB_ED_CFG, ED_SAMPLE_AVG | CCA_MODE_MASK | 0xFFu,
              CCA_MODE_ED | (uint8_t)CCA_DBM);
        field(ZB_TXPOWER, 0x1Fu, power_index());
        field(ZB_EVENT_EN, EV_ALL, EV_ALL);
        field(ZB_RX_ABORT_EN, ABORT_EVENTS, ABORT_EVENTS);
        mac_stop();
        active = 1;
#if TIKU_C5_154_COEX
        esp_coex_ieee802154_status_enable();
#endif
#if TIKU_C5_154_TRACE
        TIKU_PRINTF("154 C5: date %08lx clk %08lx clk1 %08lx map %08lx "
                    "ed_cfg %08lx event_en %08lx power %lu\n",
                    (unsigned long)TIKU_C5_REG_READ(ZB_DATE),
                    (unsigned long)TIKU_C5_REG_READ(SYSCON_CLK),
                    (unsigned long)TIKU_C5_REG_READ(SYSCON_CLK1),
                    (unsigned long)TIKU_C5_REG_READ(SYSCON_MAP),
                    (unsigned long)TIKU_C5_REG_READ(ZB_ED_CFG),
                    (unsigned long)TIKU_C5_REG_READ(ZB_EVENT_EN),
                    (unsigned long)TIKU_C5_REG_READ(ZB_TXPOWER));
#endif
    }
    tiku_ieee154_arch_set_channel(channel);
    return 0;
}

void tiku_ieee154_arch_leave(void)
{
    if (!active) {
        return;
    }
#if TIKU_C5_154_COEX
    esp_coex_ieee802154_status_disable();
#endif
    mac_stop();
    tiku_cpu_c5_delay_us(STOP_SETTLE_US);
    radio_off();
    active = 0;
}

/* The channel field holds MHz above 2400 minus 2: channel k is
 * 2405 + 5 (k - 11) MHz and the field is 5 (k - 11) + 3. */
void tiku_ieee154_arch_set_channel(uint8_t channel)
{
    cur_chan = clamp(channel);
    if (active) {
        field(ZB_CHANNEL, 0x7Fu, 5u * (uint32_t)(cur_chan - 11u) + 3u);
    }
}

/** @brief Run one energy measurement of @p symbols; 0 when it ended. */
static int measure(uint32_t symbols)
{
    mac_stop();
    field(ZB_ED_DURATION, 0xFFFFFFu, symbols);
    TIKU_C5_REG_WRITE(ZB_CMD, CMD_ED_START);
    if (!wait_events(EV_ED_DONE, WAIT_SPINS)) {
        mac_stop();
        return -1;
    }
    return 0;
}

int tiku_ieee154_arch_ed(uint8_t channel, int8_t *dbm)
{
    int level;
    int8_t rss;
    if (tiku_ieee154_arch_mode_154(channel) != 0 || measure(ED_SYMBOLS)) {
        return -1;
    }
    rss = (int8_t)((TIKU_C5_REG_READ(ZB_ED_CFG) >> 16) & 0xFFu);
    mac_stop();
    if (dbm) {
        *dbm = rss;
    }
    level = (int)rss - ED_FLOOR_DBM;
    if (level < 0) {
        level = 0;
    }
    return level > 255 ? 255 : level;
}

int tiku_ieee154_arch_cca(void)
{
    int idle;
    if (!active || measure(CCA_SYMBOLS)) {
        return 0;
    }
    idle = !(TIKU_C5_REG_READ(ZB_ED_CFG) & CCA_BUSY);
    mac_stop();
    return idle;
}

/* The MAC layer above waits for ACKs itself, so the MAC block sends and goes
 * idle without listening for one. */
int tiku_ieee154_arch_tx(const uint8_t *psdu, uint8_t len)
{
    uint32_t done;
    if (len > TIKU_154_MAX_PSDU) {
        return -1;
    }
    if (!active) {
        return -2;
    }
    mac_stop();
    field(ZB_CTRL, CTRL_ACK_RX, 0);
    tx_frame[0] = (uint8_t)(len + 2u);
    memcpy(&tx_frame[1], psdu, len);
    TIKU_C5_REG_WRITE(ZB_TXDMA_ADDR, (uint32_t)(uintptr_t)tx_frame);
    scene(1);
    TIKU_C5_REG_WRITE(ZB_CMD, CMD_TX_START);
    done = wait_events(EV_TX_DONE | EV_TX_ABORT, WAIT_SPINS);
#if TIKU_C5_154_COEX
    /* A frame the arbiter took the radio from: told, so it schedules the
     * MAC layer's retry; the reason is read before the stop clears it. */
    if ((done & EV_TX_ABORT) &&
        ((TIKU_C5_REG_READ(ZB_TX_STATUS) >> 4) & 0x1Fu) == ABORT_COEX) {
        esp_coex_ieee802154_coex_break_notify();
    }
#endif
    mac_stop();
    scene(0);
    return (done & EV_TX_DONE) ? 0 : -2;
}

/**
 * @brief Receive one frame into @p buf with the MAC's filter and ACKs as
 *        @p ctrl sets them; an ACK the MAC sends is waited for.
 * @return bytes copied, 0 on timeout or another abort, -1 for a bad FCS
 */
static int receive(uint32_t ctrl, uint8_t *buf, uint8_t cap,
                   uint32_t timeout_ms, int8_t *rssi, uint8_t *did_ack)
{
    tiku_clock_time_t start = tiku_clock_time();
    tiku_clock_time_t window =
        (tiku_clock_time_t)(((uint32_t)TIKU_CLOCK_SECOND * timeout_ms) / 1000u);
    uint32_t spin, event = 0;
    uint8_t len;
    if (!active) {
        return 0;
    }
    mac_stop();
    field(ZB_CTRL, CTRL_ACK_TX | CTRL_PROMISC | CTRL_INF0, ctrl);
    rx_frame[0] = 0;
    TIKU_C5_REG_WRITE(ZB_RXDMA_ADDR, (uint32_t)(uintptr_t)rx_frame);
    scene(1);
    TIKU_C5_REG_WRITE(ZB_CMD, CMD_RX_START);
    for (spin = 1;; spin++) {
        event = TIKU_C5_REG_READ(ZB_EVENT) & (EV_RX_DONE | EV_RX_ABORT);
        if (event) {
            break;
        }
        if ((spin & 0xFFFu) == 0u) {
            tiku_watchdog_kick();
            if ((tiku_clock_time_t)(tiku_clock_time() - start) >= window) {
                break;
            }
        }
    }
    if (!(event & EV_RX_DONE)) {
        int crc = (event & EV_RX_ABORT) &&
                  ((TIKU_C5_REG_READ(ZB_RX_STATUS) >> 4) & 0x1Fu) == ABORT_CRC;
        if (rssi && !did_ack) {
            *rssi = (int8_t)(bt_bb_get_cur_rx_info() & 0xFFu);
        }
        mac_stop();
        scene(0);
        return crc ? -1 : 0;
    }
    /* A frame that asked for an ACK and passed the filter gets one from the
     * MAC block within the turnaround; stopping earlier would cut it off. */
    len = (uint8_t)(rx_frame[0] & 0x7Fu);
    if (did_ack) {
        *did_ack = (ctrl & CTRL_ACK_TX) && len >= 3u && (rx_frame[1] & 0x20u) &&
                   (wait_events(EV_ACK_TX_DONE | EV_RX_ABORT, ACK_SPINS) &
                    EV_ACK_TX_DONE);
        if (*did_ack) {
            tiku_cpu_c5_delay_us(ACK_SETTLE_US);
        }
#if TIKU_C5_154_TRACE
        if (!*did_ack) {
            TIKU_PRINTF("154 C5 noack: len %u fcf %02x%02x seq %u ev %08lx "
                        "rx %08lx tx %08lx txrx %08lx ctrl %08lx\n",
                        (unsigned)len, rx_frame[2], rx_frame[1], rx_frame[3],
                        (unsigned long)TIKU_C5_REG_READ(ZB_EVENT),
                        (unsigned long)TIKU_C5_REG_READ(ZB_RX_STATUS),
                        (unsigned long)TIKU_C5_REG_READ(ZB_BASE + 0x084u),
                        (unsigned long)TIKU_C5_REG_READ(ZB_BASE + 0x088u),
                        (unsigned long)TIKU_C5_REG_READ(ZB_CTRL));
        }
#endif
    }
    mac_stop();
    scene(0);
    if (len < 2u) {
        return 0;
    }
    if (rssi) {
        *rssi = (int8_t)rx_frame[len - 1u];
    }
    len = (uint8_t)(len - 2u);
    if (len > cap) {
        len = cap;
    }
    memcpy(buf, &rx_frame[1], len);
    return (int)len;
}

/* Promiscuous and without ACKs: every frame with a good FCS on the channel. */
int tiku_ieee154_arch_rx(uint8_t *buf, uint8_t cap, uint32_t timeout_ms,
                         int8_t *rssi)
{
    return receive(CTRL_PROMISC, buf, cap, timeout_ms, rssi, NULL);
}

/* The MAC block filters on interface 0: frames for @p my_pan and @p my_addr
 * or broadcast reach the caller, others are dropped as they arrive. */
int tiku_ieee154_arch_rx_ack(uint8_t *buf, uint8_t cap, uint32_t timeout_ms,
                             int8_t *rssi, uint16_t my_pan, uint16_t my_addr,
                             uint8_t *did_ack)
{
    uint8_t acked = 0;
    int n;
    if (active) {
        TIKU_C5_REG_WRITE(ZB_INF0_ADDR, my_addr);
        TIKU_C5_REG_WRITE(ZB_INF0_PAN, my_pan);
    }
    n = receive(CTRL_ACK_TX | CTRL_INF0, buf, cap, timeout_ms, rssi, &acked);
    if (n != 0 && did_ack) {
        *did_ack = acked;
    }
    return n;
}

/* No timing hold is needed: the MAC block sequences its own ramps. */
void tiku_ieee154_arch_hold(int on)
{
    (void)on;
}

int tiku_ieee154_arch_ccm_star(int decrypt, const uint8_t key[16],
                               const uint8_t nonce[13], const uint8_t *aad,
                               size_t aad_len, const uint8_t *m, size_t m_len,
                               uint8_t mic_len, uint8_t *out, uint8_t *mic)
{
    return tiku_kits_crypto_ccm_star(decrypt, key, nonce, aad, aad_len, m,
                                     m_len, mic_len, out, mic) ==
                   TIKU_KITS_CRYPTO_OK
               ? 0
               : -1;
}
