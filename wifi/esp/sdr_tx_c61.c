/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * sdr_tx_c61.c - bounded IQ10 playback with exclusive C61 PHY ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <string.h>
#include <arch/esp32c61/tiku_cpu_freq_boot_arch.h>
#include <arch/esp32c61/tiku_crt_early.h>
#include <arch/esp32c61/tiku_irq_arch.h>
#include <drivers/wifi/esp/tiku_drv_sdr_esp.h>
#include <drivers/wifi/esp/tiku_drv_sdr_tx_esp.h>
#include "esp_port.h"
#include "sdr_tx_sine.h"

#ifndef TIKU_C61_TX_DATA
#define TIKU_C61_TX_DATA ((uint32_t *)0x40830000u)
#endif
#ifndef TIKU_C61_TX_FENCE
#define TIKU_C61_TX_FENCE() __asm__ volatile("fence rw, rw" ::: "memory")
#endif
#ifndef TIKU_C61_TX_READ
#define TIKU_C61_TX_READ(a) TIKU_REG32(a)
#define TIKU_C61_TX_WRITE(a,v) (TIKU_REG32(a) = (v))
#define TIKU_C61_TX_IRQ_SAVE() tiku_esp32c61_mie_off()
#define TIKU_C61_TX_IRQ_RESTORE(s) tiku_esp32c61_mie_restore(s)
#define TIKU_C61_TX_IN_ISR() tiku_esp32c61_in_isr()
#define TIKU_C61_TX_TIMER() tiku_cpu_esp32c61_systimer()
#endif
#define OWNER 0x60095004u
#define CAPTURE 0x600a9004u
#define SOURCE 0x600a9008u
#define PLAY 0x600a900cu
#define CLOCK 0x600a9c04u
#define WIFI_CLOCK 0x600a9c14u
#define NCO1 0x600a041cu
#define NCO2 0x600a0420u
#define RUN (1u << 31)
#define LOOP (1u << 19)
#define OWNER_MASK 0x11f00u
#define GUARD 0x5aa5c33cu
#define TIMER_MASK ((1ULL << 52) - 1u)

extern void phy_mac_enable_bb(unsigned);
extern void phy_set_rxclk_en(unsigned);
extern void phy_dac_rate_set(unsigned);

static uint32_t loaded, expected;
static uint8_t held, changing, faulted;
static const uint32_t register_address[] = {
    OWNER, CAPTURE, SOURCE, PLAY, CLOCK, WIFI_CLOCK,
    0x600a0448u, 0x600a0890u, NCO1, NCO2, 0x600a040cu
};

unsigned tiku_drv_sdr_tx_esp_bands(const tiku_sdr_tx_band_t **bands)
{
    static const tiku_sdr_tx_band_t channels[] = {{2412, 2462, 5}};
    if (bands) { *bands = channels; }
    return sizeof channels / sizeof *channels;
}

/** @brief Serialize calls from the kernel foreground with interrupts enabled. */
static int enter(void)
{
    uint32_t state = TIKU_C61_TX_IRQ_SAVE();
    int ok = (state & 8u) && !changing && !faulted && !TIKU_C61_TX_IN_ISR()
             && tiku_thread_in_kernel();
    if (ok) { changing = 1; }
    TIKU_C61_TX_IRQ_RESTORE(state);
    return ok;
}

/** @brief End the serialized operation. */
static void leave(void)
{
    uint32_t state = TIKU_C61_TX_IRQ_SAVE();
    changing = 0;
    TIKU_C61_TX_IRQ_RESTORE(state);
}

int tiku_drv_sdr_tx_esp_held(void) { return held; }

void tiku_drv_sdr_tx_esp_status(uint32_t *written, uint32_t *total)
{
    if (written) { *written = loaded; }
    if (total) { *total = expected; }
}

/** @brief Claim the allocator's complete hardware bank for a new upload. */
static int begin(uint32_t words)
{
    if (!espw_core_idle() ||
        (!held && tiku_drv_sdr_esp_reserved())) { return TIKU_SDR_TX_BUSY; }
    if (!held && tiku_drv_sdr_esp_reserve() != 0) { return TIKU_SDR_TX_BUSY; }
    held = 1;
    loaded = 0;
    expected = words;
    return 0;
}

int tiku_drv_sdr_tx_esp_begin(uint32_t words)
{
    int rc;
    if (!words || words > TIKU_SDR_TX_WORDS_MAX || words % TIKU_SDR_TX_ALIGN) {
        return TIKU_SDR_TX_PARAM;
    }
    if (!enter()) { return faulted ? TIKU_SDR_TX_FAULT : TIKU_SDR_TX_BUSY; }
    rc = begin(words);
    leave();
    return rc;
}

int tiku_drv_sdr_tx_esp_write(uint32_t offset, const uint32_t *samples, uint32_t words)
{
    uint32_t n;
    int rc = TIKU_SDR_TX_PARAM;
    if (!samples || !words || words > TIKU_SDR_TX_WORDS_MAX) { return rc; }
    if (!enter()) { return faulted ? TIKU_SDR_TX_FAULT : TIKU_SDR_TX_BUSY; }
    if (!held || !tiku_drv_sdr_esp_reserved() || offset != loaded ||
        loaded > expected || words > expected - loaded) { goto done; }
    for (n = 0; n < words; n++) {
        if (samples[n] & 0xfff00000u) { goto done; }
    }
    memmove(TIKU_C61_TX_DATA + offset, samples, words * sizeof(*samples));
    loaded += words;
    rc = 0;
done:
    leave();
    return rc;
}

/** @brief Expand one Q15 sine quarter into a signed sample amplitude. */
static int sinusoid(unsigned phase, unsigned amplitude)
{
    unsigned quadrant = (phase >> 8) & 3u, index = phase & 255u;
    int value = tx_sine[(quadrant & 1u) ? 256u - index : index];
    value = (value * (int)amplitude + 16383) / 32767;
    return quadrant >= 2u ? -value : value;
}

int tiku_drv_sdr_tx_esp_tone(int cycles, unsigned amplitude)
{
    uint32_t n;
    int rc;
    if (cycles < -128 || cycles > 128 || !amplitude || amplitude > 128) {
        return TIKU_SDR_TX_PARAM;
    }
    if (!enter()) { return faulted ? TIKU_SDR_TX_FAULT : TIKU_SDR_TX_BUSY; }
    rc = begin(1024);
    if (rc == 0) {
        for (n = 0; n < 1024; n++) {
            unsigned phase = (n * cycles) & 1023u;
            uint32_t i = (uint32_t)sinusoid((phase + 256) & 1023u, amplitude) & 1023u;
            uint32_t q = (uint32_t)sinusoid(phase, amplitude) & 1023u;
            TIKU_C61_TX_DATA[n] = i | (q << 10);
        }
        loaded = 1024;
    }
    leave();
    return rc;
}

int tiku_drv_sdr_tx_esp_clear(void)
{
    int rc = 0;
    if (!enter()) { return faulted ? TIKU_SDR_TX_FAULT : TIKU_SDR_TX_BUSY; }
    if (held) {
        if (!espw_core_idle()) { rc = TIKU_SDR_TX_BUSY; }
        else {
            held = 0;
            tiku_drv_sdr_esp_release();
            if (tiku_drv_sdr_esp_reserved()) { held = 1; rc = TIKU_SDR_TX_IO; }
            else { loaded = expected = 0; }
        }
    }
    leave();
    return rc;
}

/** @brief Hash the loaded waveform while the CPU owns the bank. */
static uint32_t checksum(void)
{
    uint32_t sum = 2166136261u, n;
    for (n = 0; n < loaded; n++) { sum = (sum ^ TIKU_C61_TX_DATA[n]) * 16777619u; }
    return sum;
}

/** @brief Stop playback and RF before restoring the saved registers. */
static int stop(const uint32_t *saved, int prepared)
{
    unsigned n;
    TIKU_C61_TX_WRITE(PLAY, 0);
    TIKU_C61_TX_WRITE(CAPTURE, 0);
    TIKU_C61_TX_FENCE();
    if (prepared) {
        (void)phy_stop_tx_tone(1);
        phy_force_pwr_index(0, 0);
        phy_txcal_work_mode();
        phy_pbus_xpd_tx_off();
        phy_set_txclk_en(0);
        phy_set_rxclk_en(0);
        phy_pbus_workmode();
    }
    if (TIKU_C61_TX_READ(PLAY) & RUN) { faulted = 1; }
    if (!faulted) {
        for (n = 0; n < 11; n++) { TIKU_C61_TX_WRITE(register_address[n], saved[n]); }
        TIKU_C61_TX_FENCE();
    }
    espw_phy_disable();
    espw_modem_wifi_clock_off();
    espw_core_down(ESPW_RADIO_SDR);
    if (!espw_core_idle()) { faulted = 1; }
    return faulted ? TIKU_SDR_TX_FAULT : 0;
}

/** @brief Bound playback by the 16 MHz timer and an independent polling cap. */
static int wait_until(uint64_t first, uint32_t ms, uint32_t *elapsed)
{
    uint64_t now;
    unsigned n;
    for (n = 0; n < 10000000u; n++) {
        now = TIKU_C61_TX_TIMER();
        if (!now) { return TIKU_SDR_TX_IO; }
        *elapsed = (uint32_t)(((now - first) & TIMER_MASK) / 16u);
        if (*elapsed >= ms * 1000u) { return 0; }
    }
    return TIKU_SDR_TX_IO;
}

int tiku_drv_sdr_tx_esp_send(uint32_t mhz, uint32_t ms,
                            void (*started)(void *), void *context,
                            tiku_sdr_tx_result_t *out)
{
    uint32_t saved[11], sum, n;
    uint64_t first;
    int rc = TIKU_SDR_TX_BUSY, cleanup, prepared = 0;
    if (out) { memset(out, 0, sizeof(*out)); }
    if (!out || !tiku_sdr_tx_frequency(mhz) ||
        !ms || ms > TIKU_SDR_TX_MS_MAX) { return TIKU_SDR_TX_PARAM; }
    if (!enter()) { return faulted ? TIKU_SDR_TX_FAULT : TIKU_SDR_TX_BUSY; }
    if (!held || !tiku_drv_sdr_esp_reserved() || !loaded || loaded != expected ||
        !espw_core_idle()) { goto done; }
    if ((TIKU_C61_TX_READ(OWNER) & OWNER_MASK) ||
        (TIKU_C61_TX_READ(CAPTURE) & RUN) || (TIKU_C61_TX_READ(PLAY) & RUN)) { goto done; }
    rc = TIKU_SDR_TX_IO;
    if (!TIKU_C61_TX_TIMER()) { goto done; }
    if (espw_core_up(ESPW_RADIO_SDR, 32768, 0) != 0) {
        if (!espw_core_idle()) { faulted = 1; rc = TIKU_SDR_TX_FAULT; }
        goto done;
    }
    espw_modem_wifi_clock_on();
    espw_phy_enable();
    for (n = 0; n < 11; n++) { saved[n] = TIKU_C61_TX_READ(register_address[n]); }
    if (espw_phy_cal_result(NULL, NULL) != 0) { goto finish; }
    rc = TIKU_SDR_TX_BUSY;
    if ((saved[0] & OWNER_MASK) || (saved[1] & RUN) || (saved[3] & RUN)) { goto finish; }
    for (n = 0; n < 4; n++) { TIKU_C61_TX_DATA[loaded + n] = GUARD; }
    sum = checksum();
    phy_wifi_enable_set(1);
    phy_mac_enable_bb(1);
    phy_chip_set_chan(mhz, 0);
    phy_txcal_debuge_mode_();
    prepared = 1;
    phy_force_pwr_index(1, 96);
    (void)phy_start_tx_tone_step(1, 0, 0, 0, 0, 0);
    TIKU_C61_TX_WRITE(NCO1, TIKU_C61_TX_READ(NCO1) & ~(1u << 18));
    TIKU_C61_TX_WRITE(NCO2, TIKU_C61_TX_READ(NCO2) & ~(1u << 18));
    phy_dac_rate_set(0);
    phy_set_txclk_en(1);
    phy_set_rxclk_en(0);
    TIKU_C61_TX_WRITE(WIFI_CLOCK, saved[5] | 0x3be5ffu);
    TIKU_C61_TX_WRITE(CLOCK, saved[4] | RUN | (1u << 21));
    TIKU_C61_TX_WRITE(SOURCE, (saved[2] & ~(15u << 21)) | (1u << 21));
    TIKU_C61_TX_WRITE(CAPTURE, 0);
    TIKU_C61_TX_WRITE(OWNER, (saved[0] & ~OWNER_MASK) | (1u << 11));
    TIKU_C61_TX_FENCE();
    rc = TIKU_SDR_TX_IO;
    out->owner = TIKU_C61_TX_READ(OWNER);
    if ((out->owner & OWNER_MASK) != (1u << 11) ||
        (TIKU_C61_TX_READ(NCO1) & (1u << 18)) ||
        (TIKU_C61_TX_READ(NCO2) & (1u << 18))) { goto finish; }
    first = TIKU_C61_TX_TIMER();
    if (!first) { goto finish; }
    TIKU_C61_TX_WRITE(PLAY, loaded | LOOP | RUN);
    TIKU_C61_TX_FENCE();
    out->play = TIKU_C61_TX_READ(PLAY);
    rc = TIKU_SDR_TX_UNSUPPORTED;
    if ((out->play & (RUN | LOOP | 0x3fffu)) != (loaded | LOOP | RUN)) { goto finish; }
    out->hz = TIKU_SDR_TX_HZ;
    out->words = loaded;
    if (started) { started(context); }
    rc = wait_until(first, ms, &out->elapsed_us);
finish:
    cleanup = stop(saved, prepared);
    if (cleanup) { rc = cleanup; }
    else if (prepared) {
        int damaged = checksum() != sum;
        for (n = 0; n < 4; n++) {
            if (TIKU_C61_TX_DATA[loaded + n] != GUARD) { damaged = 1; }
        }
        if (damaged) { loaded = 0; rc = TIKU_SDR_TX_GUARD; }
    }
done:
    leave();
    return rc;
}
