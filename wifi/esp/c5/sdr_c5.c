/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * sdr_c5.c - C5 bounded receive-only ADC snapshots.
 * Register fields follow ESP-IDF 4d59230 HP_SYSTEM/MODEM_SYSCON and the
 * pinned Espressif PHY 20f1db0 RF-test binary's ADC dump sequence.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "sdr_c5.h"
#include "tiku_drv_phy_c5.h"
#include <stdbool.h>
#include <arch/esp32c5/tiku_esp32c5_regs.h>
#include <arch/esp32c5/tiku_irq_arch.h>
#include <arch/esp32c5/tiku_systimer_arch.h>
#include <arch/esp32c5/tiku_cpu_common.h>
#include <drivers/wifi/esp/tiku_drv_sdr_esp.h>
#if (TIKU_THREADS_ENABLE + 0)
#include <kernel/threads/tiku_thread.h>
#endif
#if (TIKU_C5_SDR_TRACE + 0)
#include <hal/tiku_printf_hal.h>
static uint32_t dump_status;
#endif

#define OWNER       0x60095004u
#define CTRL        0x600A9004u
#define MODE        0x600A9008u
#define PACK        0x600A9018u
#define CLOCKS      0x600A9C04u
#define WIFI_CLOCKS 0x600A9C14u
#define GATE_A      0x600A0800u
#define GATE_B      0x600A20B4u
#define RUN         (1u << 31)
#define TRIGGER     (1u << 19)
#define DONE        (1u << 18)
#define SENTINEL    0xa5a0055au
#define OWNER_BITS  ((15u << 8) | (1u << 16))
#define DUMP_OWNER  ((1u << 9) | (1u << 16))
#define DUMP_CLOCKS ((1u << 31) | (1u << 21))
#ifndef TIKU_C5_SDR_FENCE
#define TIKU_C5_SDR_FENCE() __asm__ volatile ("fence rw, rw" ::: "memory")
#endif
#ifndef TIKU_C5_SDR_DATA
#define TIKU_C5_SDR_DATA ((volatile uint32_t *)C5_SDR_DATA_BASE)
#endif

/* These entry points are provided by the pinned C5 libphy archive. */
extern void phy_set_chanfreq(unsigned mhz, unsigned mode);
extern void phy_force_rx_gain(unsigned enable, unsigned index);
extern void phy_pbus_workmode(void);
extern void phy_mac_enable_bb(unsigned on);
extern void set_bb_wdg(bool, bool, unsigned, unsigned, bool, bool, bool);
extern void phy_pbus_xpd_tx_off(void);
extern void phy_set_rxclk_en(unsigned on);
extern int phy_stop_tx_tone(unsigned arg);

static uint32_t saved_wifi;
static uint8_t active, changing;

/** @brief Serialize foreground calls with interrupts enabled. */
static int enter(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    int ok = (state & 8u) && !changing
#if (TIKU_THREADS_ENABLE + 0)
        && tiku_thread_in_kernel()
#endif
        ;
    if (ok) { changing = 1; }
    TIKU_C5_IRQ_RESTORE(state);
    return ok;
}

/** @brief End the serialized foreground operation. */
static void leave(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    changing = 0;
    TIKU_C5_IRQ_RESTORE(state);
}

int tiku_sdr_c5_frequency(uint32_t mhz)
{
    return (mhz >= 2400u && mhz <= 2500u) || (mhz >= 4900u && mhz <= 5900u);
}

int tiku_sdr_c5_active(void) { return active; }

int tiku_sdr_c5_power(int on)
{
    uint32_t state;
    if (!enter()) { return -1; }
    if (!!on == !!active) { leave(); return 0; }
    if (on) {
        if (!tiku_drv_sdr_esp_reserved() || tiku_drv_phy_c5_active() ||
            tiku_drv_phy_c5_on() != 0) { leave(); return -1; }
        state = TIKU_C5_IRQ_SAVE();
        saved_wifi = TIKU_C5_REG_READ(WIFI_CLOCKS);
        TIKU_C5_REG_WRITE(WIFI_CLOCKS, saved_wifi | 0x7ffu);
        TIKU_C5_IRQ_RESTORE(state);
        active = 1;
    } else {
        phy_force_rx_gain(0, 0);
        phy_pbus_xpd_tx_off();
        phy_pbus_workmode();
        if (tiku_drv_phy_c5_off() != 0) { leave(); return -1; }
        state = TIKU_C5_IRQ_SAVE();
        TIKU_C5_REG_WRITE(WIFI_CLOCKS,
            (TIKU_C5_REG_READ(WIFI_CLOCKS) & ~0x7ffu) | (saved_wifi & 0x7ffu));
        TIKU_C5_IRQ_RESTORE(state);
        active = 0;
    }
    leave();
    return 0;
}

/** @brief Run a bounded dump with IRQs masked; restore register ownership before returning. */
static int dump(uint32_t words, uint8_t rate, uint32_t *us)
{
    const uint32_t addresses[] = {OWNER, MODE, PACK, CLOCKS, GATE_A, GATE_B};
    uint32_t saved[6], ctrl, n;
    uint64_t start, now;
    int done = 0;
    for (n = 0; n < 6; n++) { saved[n] = TIKU_C5_REG_READ(addresses[n]); }
    ctrl = TIKU_C5_REG_READ(CTRL);
    if ((saved[0] & OWNER_BITS) || (ctrl & RUN)) { return -1; }
    TIKU_C5_REG_WRITE(CLOCKS, saved[3] | DUMP_CLOCKS);
    TIKU_C5_REG_WRITE(GATE_A, saved[4] | (1u << 2));
    TIKU_C5_REG_WRITE(GATE_B, saved[5] & ~1u);
    TIKU_C5_REG_WRITE(MODE, (saved[1] & ~(127u << 17)) | (15u << 17) | ((uint32_t)rate << 21));
    TIKU_C5_REG_WRITE(PACK, (saved[2] & ~0x1ffffffu) |
        24u | (25u << 6) | (26u << 12) | (27u << 18) | (1u << 24));
    TIKU_C5_REG_WRITE(OWNER, saved[0] | DUMP_OWNER);
    TIKU_C5_SDR_FENCE();
    if ((TIKU_C5_REG_READ(OWNER) & OWNER_BITS) != DUMP_OWNER) { goto restore; }
    /* DONE is W1C; bit 17 must remain clear for a finite C5 capture. */
    TIKU_C5_REG_WRITE(CTRL, words | DONE);
    TIKU_C5_REG_WRITE(CTRL, words);
    if (tiku_c5_systimer_read(&start) != 0) { goto restore; }
    TIKU_C5_REG_WRITE(CTRL, words | RUN);
    TIKU_C5_REG_WRITE(CTRL, words | RUN | TRIGGER);
    TIKU_C5_REG_WRITE(CTRL, words | RUN);
    for (n = 0; n < 1000000u; n++) {
        if (tiku_c5_systimer_read(&now) != 0) { break; }
        *us = (uint32_t)(((now - start) & TIKU_C5_SYSTIMER_MASK) / 16u);
        if (TIKU_C5_REG_READ(CTRL) & DONE) { done = 1; break; }
        if (*us >= 20000u) { break; }
    }
restore:
#if (TIKU_C5_SDR_TRACE + 0)
    dump_status = TIKU_C5_REG_READ(CTRL);
#endif
    TIKU_C5_REG_WRITE(CTRL, 0);
    TIKU_C5_SDR_FENCE();
    for (n = 0; n < 6; n++) { TIKU_C5_REG_WRITE(addresses[n], saved[n]); }
    TIKU_C5_SDR_FENCE();
    return done ? 0 : -2;
}

int tiku_sdr_c5_capture(uint32_t mhz, uint8_t rate, uint32_t words,
                        uint32_t *us, int gain)
{
    volatile uint32_t *data = TIKU_C5_SDR_DATA;
    uint32_t n, state, limit, different = 0;
    int rc;
    if (!active || !tiku_drv_sdr_esp_reserved() || !us ||
        !tiku_sdr_c5_frequency(mhz) || rate >= TIKU_DRV_SDR_ESP_RATES ||
        words == 0 || words > TIKU_DRV_SDR_ESP_WORDS_MAX || gain < -1 || !enter()) { return -1; }
    *us = 0;
    if ((TIKU_C5_REG_READ(OWNER) & OWNER_BITS) ||
        (TIKU_C5_REG_READ(CTRL) & RUN)) { leave(); return -1; }
    for (n = 0; n < words + 4; n++) { data[n] = SENTINEL; }
    set_bb_wdg(true, false, 0x18, 0xaa, false, false, false);
    phy_mac_enable_bb(1);
    phy_set_chanfreq(mhz, rate == 0 ? 1u : 0u);
    (void)phy_stop_tx_tone(1);
    phy_pbus_workmode();
    phy_pbus_xpd_tx_off();
    phy_set_rxclk_en(1);
    limit = (TIKU_C5_REG_READ(0x600A702Cu) >> 8) & 127u;
    if (limit > 89u) { limit = 89u; }
    if (gain >= 0 && (unsigned)gain > limit) { gain = (int)limit; }
    state = TIKU_C5_IRQ_SAVE();
    if (gain >= 0) { phy_force_rx_gain(1, (unsigned)gain); }
    rc = dump(words, rate, us);
    if (gain >= 0) { phy_force_rx_gain(0, 0); }
    TIKU_C5_IRQ_RESTORE(state);
#if (TIKU_C5_SDR_TRACE + 0)
    TIKU_PRINTF("SDR debug ctrl=%08lx first=%08lx last=%08lx us=%lu gainmax=%lu\n",
        (unsigned long)dump_status, (unsigned long)data[0],
        (unsigned long)data[words - 1], (unsigned long)*us, (unsigned long)limit);
#endif
    if (!rc) {
        for (n = 0; n < words; n++) {
            if (data[n] == SENTINEL) { rc = -3; }
            different |= (data[n] ^ data[0]) & 0xfffffu;
        }
        for (; n < words + 4; n++) { if (data[n] != SENTINEL) { rc = -3; } }
        if (words > 1 && !different) { rc = -3; }
    }
    leave();
    return rc;
}
