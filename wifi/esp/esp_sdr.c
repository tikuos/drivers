/*
 * Tiku Drivers - ESP32-C5/C61 raw I/Q capture and spectrum processing
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * The modem writes ADC samples into a reserved SRAM bank. The C5 hardware
 * sequence is in c5/sdr_c5.c; the C61 sequence and shared FFT are below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <kernel/memory/tiku_mem.h>
#if defined(PLATFORM_ESP32C5)
#include "c5/sdr_c5.h"
#else
#include <arch/esp32c61/tiku_cpu_freq_boot_arch.h>
#include <arch/esp32c61/tiku_esp32c61_regs.h>
#include <arch/esp32c61/tiku_irq_arch.h>
#endif

#include "esp_abi.h"
#include "esp_port.h"
#include "tiku_drv_sdr_esp.h"

/* C61 HP_SYSTEM_SRAM_USAGE_CONF: bits 12..8 select 64 KiB banks;
 * bit 16 shifts the dump address. C5 uses its separate 128 KiB bank map. */
#define SRAM_USAGE_CONF     0x60095004UL
#define SRAM_USAGE_POS      8U
#define SRAM_USAGE_MSK      (0x1FUL << SRAM_USAGE_POS)
#define SRAM_DUMP_SHIFT     (1UL << 16)
#if defined(PLATFORM_ESP32C5)
#define SRAM_BANK_BYTES     C5_SDR_BANK_BYTES
#define SRAM_BANK           1U
#define SRAM_BANK_ADDR      C5_SDR_BANK_BASE
#define SRAM_DATA_ADDR      C5_SDR_DATA_BASE
#else
#define SRAM_BANK_BYTES     0x10000UL
#define SRAM_BANK           3U              /* the one proven: 0x40830000 */
#define SRAM_BANK_ADDR      (0x40800000UL + SRAM_BANK * SRAM_BANK_BYTES)
#define SRAM_DATA_ADDR      SRAM_BANK_ADDR
#endif

/* The dump unit: a word count and run/trigger/done bits, a source and rate
 * field, a field-packing selector; and the three gates around it. */
#define DUMP_CTRL           0x600A9004UL
#define DUMP_CTRL_RUN       (1UL << 31)
#define DUMP_CTRL_TRIG      (1UL << 19)
#define DUMP_CTRL_DONE      (1UL << 18)
#define DUMP_MODE           0x600A9008UL
#define DUMP_MODE_SRC_MSK   (0x7FUL << 17)
#define DUMP_MODE_SRC_ADC   (15UL << 17)
#define DUMP_MODE_RATE_POS  21U
#define DUMP_WRITER         0x600A900CUL
#define DUMP_PACK           0x600A9018UL
#define DUMP_PACK_MSK       0x01FFFFFFUL
#define DUMP_PACK_IQ        (24UL | (25UL << 6) | (26UL << 12) | (27UL << 18) | \
                             (1UL << 24))
#define DUMP_CLOCKS         0x600A9C04UL    /* modem syscon clock gates */
#define DUMP_GATE_A         0x600A0800UL
#define DUMP_GATE_A_BIT     (1UL << 4)
#define DUMP_GATE_B         0x600A20B4UL
#define DUMP_GATE_B_BIT     (1UL << 1)

/* The baseband's first tone generator: its step's upper ten bits, its gain
 * (negated) and its enable, and the step's two low bits next door. */
#define TONE1_REG           0x600A041CUL
#define TONE1_STEP_MSK      0x3FFUL
#define TONE_LSB_REG        0x600A0428UL
#define TONE1_LSB_MSK       0x3UL

#define SENTINEL            0xA5A0055AUL
#define DONE_WAIT_US        20000UL

static tiku_arena_t sdr_bank;
static uint8_t      sdr_reserved;
static int16_t      sdr_hold = TIKU_DRV_SDR_ESP_HOLD; /* gain index, -1 AGC */
#if TIKU_DRV_SDR_ESP_PROBE
static uint8_t      sdr_source = 15U;    /* the unit's tap: 15 is the ADC */
static unsigned     fsk_a, fsk_b;         /* steps the capture toggles */
static uint32_t     fsk_half;             /* core cycles per half period */
#define SDR_SOURCE  sdr_source
#else
#define SDR_SOURCE  15U
#endif

static const uint32_t sdr_rates[TIKU_DRV_SDR_ESP_RATES] = {
    80000000UL, 40000000UL, 20000000UL, 10000000UL, 8000000UL, 4000000UL
};

uint32_t tiku_drv_sdr_esp_rate_hz(uint8_t rate) {
    return rate < TIKU_DRV_SDR_ESP_RATES ? sdr_rates[rate] : 0UL;
}

int tiku_drv_sdr_esp_reserve(void) {
    tiku_mem_request_t req = TIKU_MEM_REQUEST_DEFAULT;

    if (sdr_reserved) {
        return 0;
    }
    req.alignment = SRAM_BANK_BYTES;
    req.allocation_class = TIKU_MEM_TRANSIENT;
    if (tiku_tier_arena_create_opts(&sdr_bank, TIKU_MEM_SRAM, SRAM_BANK_BYTES,
                                    0U, &req) != TIKU_MEM_OK) {
        TIKU_PRINTF("[esp-sdr] no free %lu KiB capture bank\n",
                    (unsigned long)(SRAM_BANK_BYTES / 1024));
        return -1;
    }
    if ((uintptr_t)sdr_bank.buf != SRAM_BANK_ADDR) {
        TIKU_PRINTF("[esp-sdr] the tier lent 0x%08lx, not bank %u\n",
                    (unsigned long)(uintptr_t)sdr_bank.buf, SRAM_BANK);
        (void)tiku_mem_workspace_close(&sdr_bank);
        return -1;
    }
    sdr_reserved = 1U;
    memset((void *)SRAM_DATA_ADDR, 0, 65536U);
    return 0;
}

void tiku_drv_sdr_esp_release(void) {
    if (sdr_reserved) {
#if defined(PLATFORM_ESP32C5)
        if (tiku_sdr_c5_power(0) != 0) { return; }
#endif
        if (tiku_mem_workspace_close(&sdr_bank) == TIKU_MEM_OK) {
            sdr_reserved = 0U;
        }
    }
}

const uint32_t *tiku_drv_sdr_esp_samples(void) {
    return sdr_reserved ? (const uint32_t *)SRAM_DATA_ADDR : NULL;
}

#if !defined(PLATFORM_ESP32C5)
/** @brief Hand the bank to the dump unit (1) or back to the core (0). */
static void bank_owner(int dump) {
    uint32_t v = TIKU_REG32(SRAM_USAGE_CONF) & ~(SRAM_USAGE_MSK | SRAM_DUMP_SHIFT);

    if (dump) {
        v |= 1UL << (SRAM_USAGE_POS + SRAM_BANK);
    }
    TIKU_REG32(SRAM_USAGE_CONF) = v;
    __asm__ volatile ("fence rw, rw" ::: "memory");
    (void)TIKU_REG32(SRAM_USAGE_CONF);
}

/** @brief Run the unit for @p words at rate code @p rate; interrupts off.
 *         @return 1 when it reported done */
static int dump_run(uint32_t words, uint8_t rate) {
    uint32_t clocks = TIKU_REG32(DUMP_CLOCKS);
    uint32_t gate_a = TIKU_REG32(DUMP_GATE_A);
    uint32_t gate_b = TIKU_REG32(DUMP_GATE_B);
    uint64_t t0;
    int done = 0;

    TIKU_REG32(DUMP_CTRL) = 0UL;
    TIKU_REG32(DUMP_WRITER) = 0UL;
    TIKU_REG32(DUMP_CLOCKS) = 0xFFFFFFFFUL;
    TIKU_REG32(DUMP_GATE_A) = gate_a | DUMP_GATE_A_BIT;
    TIKU_REG32(DUMP_MODE) = (TIKU_REG32(DUMP_MODE) & ~DUMP_MODE_SRC_MSK) |
                            ((uint32_t)SDR_SOURCE << 17) |
                            ((uint32_t)rate << DUMP_MODE_RATE_POS);
    TIKU_REG32(DUMP_PACK) = (TIKU_REG32(DUMP_PACK) & ~DUMP_PACK_MSK) | DUMP_PACK_IQ;
    TIKU_REG32(DUMP_GATE_B) = gate_b & ~DUMP_GATE_B_BIT;
    bank_owner(1);

    TIKU_REG32(DUMP_CTRL) = words | DUMP_CTRL_DONE;
    TIKU_REG32(DUMP_CTRL) = words;
    TIKU_REG32(DUMP_CTRL) = words | DUMP_CTRL_RUN;
    TIKU_REG32(DUMP_CTRL) = words | DUMP_CTRL_RUN | DUMP_CTRL_TRIG;
    TIKU_REG32(DUMP_CTRL) = words | DUMP_CTRL_RUN;
    t0 = tiku_cpu_esp32c61_systimer();
#if TIKU_DRV_SDR_ESP_PROBE
    if (fsk_half != 0UL) {
        /* The modulator: the tone's step register, one precomputed store
         * per update on a cycle count, while the unit records what the
         * receiver hears of it; the unit is polled every 64 updates.  The
         * step's two low bits live next door: steps multiples of 4 here. */
        volatile uint32_t *reg = (volatile uint32_t *)TONE1_REG;
        uint32_t wa = (TIKU_REG32(TONE1_REG) & ~TONE1_STEP_MSK) |
                      ((fsk_a >> 2) & TONE1_STEP_MSK);
        uint32_t wb = (wa & ~TONE1_STEP_MSK) | ((fsk_b >> 2) & TONE1_STEP_MSK);
        uint32_t next = ESP32C61_CSR_READ(mcycle) + fsk_half;
        uint32_t n = 0UL;

        while (!done) {
            while ((int32_t)(ESP32C61_CSR_READ(mcycle) - next) < 0) {
            }
            *reg = (n & 1UL) ? wb : wa;
            next += fsk_half;
            if ((++n & 63UL) == 0UL) {
                done = (TIKU_REG32(DUMP_CTRL) & DUMP_CTRL_DONE) != 0UL;
                if (tiku_cpu_esp32c61_systimer() - t0 >= DONE_WAIT_US * 16ULL) {
                    break;
                }
            }
        }
    }
#endif
    while (!done && tiku_cpu_esp32c61_systimer() - t0 < DONE_WAIT_US * 16ULL) {
        done = (TIKU_REG32(DUMP_CTRL) & DUMP_CTRL_DONE) != 0UL;
    }
    TIKU_REG32(DUMP_CTRL) = 0UL;

    bank_owner(0);
    TIKU_REG32(DUMP_GATE_B) = gate_b;
    TIKU_REG32(DUMP_GATE_A) = gate_a;
    TIKU_REG32(DUMP_CLOCKS) = clocks;
    return done;
}

#endif

static int32_t field10(uint32_t w, unsigned pos) {
    return (int32_t)((w >> pos) << 22) >> 22;
}

static uint32_t isqrt(uint32_t v) {
    uint32_t r = 0UL, bit = 1UL << 30;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0UL) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* A quarter wave of 32 points, scaled to 1024: the DFT's twiddles. */
static const int16_t sdr_sin[9] = {
    0, 200, 392, 569, 724, 851, 946, 1004, 1024
};

static int32_t tw_sin(unsigned k) {           /* sin(2*pi*k/32) */
    k &= 31U;
    if (k <= 8U) return sdr_sin[k];
    if (k <= 16U) return sdr_sin[16U - k];
    if (k <= 24U) return -sdr_sin[k - 16U];
    return -sdr_sin[32U - k];
}

static int32_t tw_cos(unsigned k) {
    return tw_sin(k + 8U);
}

/** @brief Measure the first @p words of the bank into @p out. */
static void measure(uint32_t words, tiku_drv_sdr_esp_result_t *out) {
    const uint32_t *w = (const uint32_t *)SRAM_DATA_ADDR;
    int64_t si = 0, sq = 0;
    uint64_t pw = 0;
    uint32_t n = words, peak = 0UL, per = words / 16U;
    unsigned s;

    out->gain = (uint8_t)(w[0] >> 20);
    for (s = 0U; s < 16U; s++) {
        uint64_t acc = 0;
        uint32_t j;

        for (j = s * per; j < (s + 1U) * per; j++) {
            int32_t i = field10(w[j], 0U), q = field10(w[j], 10U);
            uint32_t ai = (uint32_t)(i < 0 ? -i : i), aq = (uint32_t)(q < 0 ? -q : q);

            si += i;
            sq += q;
            acc += (uint64_t)(i * i + q * q);
            if (ai > peak) peak = ai;
            if (aq > peak) peak = aq;
        }
        out->slice[s] = per != 0U ? (uint32_t)(acc / per) : 0UL;
        pw += acc;
    }
    n = per * 16U;
    out->dc_i = (int16_t)(n != 0U ? si / (int64_t)n : 0);
    out->dc_q = (int16_t)(n != 0U ? sq / (int64_t)n : 0);
    out->rms = (uint16_t)(n != 0U ? isqrt((uint32_t)(pw / n)) : 0U);
    out->peak = (uint16_t)peak;

    /* 32 bins over up to 1024 samples of the loudest slice, that window's
     * own mean removed, bin 16 at the centre. */
    for (s = 1U; s < 16U; s++) {
        if (out->slice[s] > out->slice[out->loud]) {
            out->loud = (uint8_t)s;
        }
    }
    {
        uint32_t m = per < 1024U ? per : 1024U, j, base = out->loud * per;
        int64_t wi = 0, wq = 0;

        for (j = 0U; j < m; j++) {
            wi += field10(w[base + j], 0U);
            wq += field10(w[base + j], 10U);
        }
        if (m != 0U) {
            wi /= (int64_t)m;
            wq /= (int64_t)m;
        }
        w += base;
        si = wi;
        sq = wq;
        n = m;
    }
    for (s = 0U; s < TIKU_DRV_SDR_ESP_BINS; s++) {
        int32_t k = (int32_t)s - (int32_t)(TIKU_DRV_SDR_ESP_BINS / 2U);
        int64_t re = 0, im = 0;
        uint32_t j, m = n;

        for (j = 0U; j < m; j++) {
            int32_t i = field10(w[j], 0U) - (int32_t)si;
            int32_t q = (int32_t)sq - field10(w[j], 10U);   /* conjugate */
            unsigned ph = (unsigned)((int32_t)j * k) & 31U;
            int32_t c = tw_cos(ph), sn = tw_sin(ph);

            re += (int64_t)(i * c + q * sn);
            im += (int64_t)(q * c - i * sn);
        }
        re /= 1024 * (int64_t)(m ? m : 1);
        im /= 1024 * (int64_t)(m ? m : 1);
        out->bin[s] = (uint32_t)(re * re + im * im);
    }
}

/** @brief Tune, then run the unit for @p words at rate code @p rate into the
 *         bank at gain index @p gain (-1: the AGC's); @p us its wall time.
 *         @return 0, -2 not done, -3 nothing */
#if defined(PLATFORM_ESP32C5)
#define capture_raw tiku_sdr_c5_capture
#else
static int capture_raw(uint32_t mhz, uint8_t rate, uint32_t words,
                       uint32_t *us, int gain) {
    volatile uint32_t *bank = (volatile uint32_t *)SRAM_BANK_ADDR;
    uint32_t m, j, t0, same = 0UL;
    int done;

    for (j = 0U; j < words + 4U; j++) {
        bank[j] = SENTINEL;
    }
    /* A channel centre through the channel path, anything else by the
     * synthesizer directly. */
    if ((mhz >= 2412UL && mhz <= 2472UL && (mhz - 2412UL) % 5UL == 0UL) ||
        mhz == 2484UL) {
        phy_chip_set_chan((unsigned)mhz, 0U);
    } else {
        phy_chip_set_chan(2412U, 0U);
        phy_set_freq((unsigned)mhz, 0);
    }

    /* The gain held for this capture alone: the AGC has the radio between. */
    m = tiku_esp32c61_mie_off();
    if (gain >= 0) {
        phy_force_rx_gain(1U, (unsigned)gain);
    }
    t0 = (uint32_t)tiku_cpu_esp32c61_systimer();
    done = dump_run(words, rate);
    *us = ((uint32_t)tiku_cpu_esp32c61_systimer() - t0) / 16U;
    if (gain >= 0) {
        phy_force_rx_gain(0U, 0U);
    }
    tiku_esp32c61_mie_restore(m);

    for (j = 0U; j < words; j++) {
        if (bank[j] == SENTINEL) {
            same++;
        }
    }
    if (!done) {
        return -2;
    }
    return (same == words || bank[words] != SENTINEL) ? -3 : 0;
}
#endif

static int sdr_ready(uint8_t rate) {
#if defined(PLATFORM_ESP32C5)
    return sdr_reserved && tiku_sdr_c5_active() && rate < TIKU_DRV_SDR_ESP_RATES;
#else
    return sdr_reserved && (espw_core_radios() & ESPW_RADIO_WIFI) != 0U &&
           rate < TIKU_DRV_SDR_ESP_RATES;
#endif
}

int tiku_drv_sdr_esp_capture(uint32_t mhz, uint8_t rate, uint32_t words,
                             tiku_drv_sdr_esp_result_t *out) {
    int rc;

    if (!out || !sdr_ready(rate) || words == 0U || words > TIKU_DRV_SDR_ESP_WORDS_MAX) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->words = words;
    out->hz = sdr_rates[rate];
    rc = capture_raw(mhz, rate, words, &out->us, sdr_hold);
    if (rc == 0) {
        measure(words, out);
    }
    return rc;
}

void tiku_drv_sdr_esp_hold(int index) {
    sdr_hold = (int16_t)(index < 0 ? -1 : index > 255 ? 255 : index);
}

int tiku_drv_sdr_esp_held(void) {
    return sdr_hold;
}

/*---------------------------------------------------------------------------*/
/* Spectrum: blocks of the capture, each windowed and transformed, their     */
/* power averaged; the bank's upper half is the scratch.                     */
/*---------------------------------------------------------------------------*/

#define SPEC_BLOCKS         32U
#define SPEC_SCRATCH        (SRAM_DATA_ADDR + 32768U)

/* A held capture with more than SPEC_RAILED samples at the ADC's rail is
 * splattered across the spectrum: it is taken again SPEC_STEP indices
 * (about as many decibels) lower. */
#define SPEC_RAIL           510
#define SPEC_RAILED         4U
#define SPEC_STEP           16

/** @brief Whether more than SPEC_RAILED of the bank's first @p words sit at
 *         the ADC's rail. */
static int railed(uint32_t words) {
    const uint32_t *w = (const uint32_t *)SRAM_DATA_ADDR;
    uint32_t j, n = 0UL;

    for (j = 0U; j < words && n <= SPEC_RAILED; j++) {
        int32_t i = field10(w[j], 0U), q = field10(w[j], 10U);

        if (i >= SPEC_RAIL || i <= -SPEC_RAIL || q >= SPEC_RAIL ||
            q <= -SPEC_RAIL) {
            n++;
        }
    }
    return n > SPEC_RAILED;
}

/* A quarter wave in 64 steps, Q15: the twiddles' and the window's source. */
static const int16_t sin_q[65] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739,
    9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151,
    16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683,
    28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571, 30852, 31113,
    31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678,
    32728, 32757, 32767
};

static int32_t sin256(unsigned i) {           /* sin(2*pi*i/256), Q15 */
    i &= 255U;
    if (i <= 64U) return sin_q[i];
    if (i <= 128U) return sin_q[128U - i];
    if (i <= 192U) return -sin_q[i - 128U];
    return -sin_q[256U - i];
}

static int32_t cos256(unsigned i) {
    return sin256(i + 64U);
}

/** @brief In place, radix 2, @p n a power of two up to 256. */
static void fft(int32_t *re, int32_t *im, unsigned n) {
    unsigned i, j = 0U, k, len, bit;

    for (i = 1U; i < n; i++) {
        for (bit = n >> 1; (j & bit) != 0U; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            int32_t t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2U; len <= n; len <<= 1) {
        unsigned half = len >> 1, step = 256U / len;

        for (i = 0U; i < n; i += len) {
            for (k = 0U; k < half; k++) {
                int32_t wr = cos256(k * step), wi = -sin256(k * step);
                int32_t *ar = &re[i + k], *ai = &im[i + k];
                int32_t *br = &re[i + k + half], *bi = &im[i + k + half];
                int32_t tr = (int32_t)(((int64_t)*br * wr - (int64_t)*bi * wi) >> 15);
                int32_t ti = (int32_t)(((int64_t)*br * wi + (int64_t)*bi * wr) >> 15);

                *br = *ar - tr;
                *bi = *ai - ti;
                *ar += tr;
                *ai += ti;
            }
        }
    }
}

/** @brief Half-decibels of @p p: 20 log10(p), log2 by its leading bit and a
 *         quadratic on the next eight. */
static uint8_t half_db(uint64_t p) {
    uint32_t msb = 0U, x, l;

    if (p < 2ULL) {
        return 0U;
    }
    while ((p >> (msb + 1U)) != 0ULL) {
        msb++;
    }
    x = msb >= 8U ? (uint32_t)(p >> (msb - 8U)) & 0xFFU
                  : (uint32_t)(p << (8U - msb)) & 0xFFU;
    l = (msb << 8) + ((x * (345U - (89U * x >> 8))) >> 8);
    l = (l * 1541U + 32768U) >> 16;
    return (uint8_t)(l > 255U ? 255U : l);
}

int tiku_drv_sdr_esp_spectrum(uint32_t mhz, uint8_t rate, unsigned nfft,
                              uint8_t *db, uint8_t *gain) {
    int32_t *re = (int32_t *)SPEC_SCRATCH;
    int32_t *im = re + 256;
    uint64_t *acc = (uint64_t *)(im + 256);
    const uint32_t *w = (const uint32_t *)SRAM_DATA_ADDR;
    uint32_t us, words = nfft * SPEC_BLOCKS;
    unsigned b, j, step;
    int rc, held = sdr_hold;

    if (!db || !gain || !sdr_ready(rate) || (nfft != 64U && nfft != 128U && nfft != 256U)) {
        return -1;
    }
    for (;;) {
        rc = capture_raw(mhz, rate, words, &us, held);
        if (rc != 0) {
            return rc;
        }
        if (held < SPEC_STEP || !railed(words)) {
            break;
        }
        held -= SPEC_STEP;
    }
    *gain = (uint8_t)(w[0] >> 20);
    step = 256U / nfft;
    memset(acc, 0, nfft * sizeof *acc);
    for (b = 0U; b < SPEC_BLOCKS; b++) {
        const uint32_t *blk = w + b * nfft;
        int32_t mi = 0, mq = 0;

        for (j = 0U; j < nfft; j++) {
            mi += field10(blk[j], 0U);
            mq += field10(blk[j], 10U);
        }
        mi /= (int32_t)nfft;
        mq /= (int32_t)nfft;
        for (j = 0U; j < nfft; j++) {
            /* Hann: (1 - cos) / 2, Q15. */
            int32_t win = (32767 - cos256(j * step)) >> 1;

            re[j] = ((field10(blk[j], 0U) - mi) * win) >> 15;
            im[j] = -(((field10(blk[j], 10U) - mq) * win) >> 15);
        }
        fft(re, im, nfft);
        for (j = 0U; j < nfft; j++) {
            acc[j] += (uint64_t)((int64_t)re[j] * re[j] + (int64_t)im[j] * im[j]);
        }
    }
    /* Low to high frequency: the negative half first.  The unit's Q runs
     * opposite to the air (BLE's 2402/2426/2480 showed mirrored), hence the
     * conjugate above. */
    for (j = 0U; j < nfft; j++) {
        db[j] = half_db(acc[(j + nfft / 2U) % nfft] / SPEC_BLOCKS);
    }
    return 0;
}

int tiku_drv_sdr_esp_reserved(void) {
    return sdr_reserved;
}

/*---------------------------------------------------------------------------*/
/* The transmit side, as found: lab probes, built with                       */
/* TIKU_DRV_SDR_ESP_PROBE=1 alone.                                           */
/*---------------------------------------------------------------------------*/
#if TIKU_DRV_SDR_ESP_PROBE

int tiku_drv_sdr_esp_tone(int on, unsigned xpd_a, unsigned xpd_b,
                          unsigned pwr, unsigned step, int gain) {
    int rc;

    if (!on) {
        rc = phy_stop_tx_tone(1U);
        phy_force_pwr_index(0U, 0U);
        phy_pbus_xpd_tx_off();
        phy_pbus_workmode();
        return rc;
    }
    phy_pbus_debugmode();
    phy_set_txclk_en(1U);
    phy_pbus_xpd_tx_on(xpd_a, xpd_b);
    phy_pbus_xpd_rx_on(1U);
    phy_force_pwr_index(1U, pwr);
    return phy_start_tx_tone_step(1U, step, gain, 0U, 0U, 0);
}

void tiku_drv_sdr_esp_loopback(int on, unsigned a, unsigned b, unsigned c) {
    if (on) {
        phy_pbus_debugmode();
    }
    phy_loopback_mode_en(on ? 1U : 0U);
    if (on) {
        phy_set_loopback_gain(a, b, c);
    } else {
        phy_pbus_workmode();
    }
}

void tiku_drv_sdr_esp_txcal(int on) {
    if (on) {
        phy_txcal_debuge_mode_();
    } else {
        phy_txcal_work_mode();
    }
}

void tiku_drv_sdr_esp_source(uint8_t source) {
    sdr_source = source & 15U;
}

int tiku_drv_sdr_esp_power_db(int tone, unsigned gain, unsigned sel) {
    return tone ? phy_meas_tone_pwr_db(gain, 0U, sel) : phy_get_power_db(sel);
}

int tiku_drv_sdr_esp_nco(int on, unsigned step, int gain) {
    return on ? phy_start_tx_tone_step(1U, step, gain, 0U, 0U, 0)
              : phy_stop_tx_tone(1U);
}

int tiku_drv_sdr_esp_fsk(uint32_t mhz, uint8_t rate, uint32_t words,
                         unsigned a, unsigned b, uint32_t half,
                         tiku_drv_sdr_esp_result_t *out) {
    int rc;

    fsk_a = a;
    fsk_b = b;
    fsk_half = half;
    rc = tiku_drv_sdr_esp_capture(mhz, rate, words, out);
    fsk_half = 0UL;
    return rc;
}

static unsigned hear_pbus, hear_i2c;

void tiku_drv_sdr_esp_hear(int on) {
    if (on) {
        phy_pbus_debugmode();
        phy_pbus_xpd_rx_on(0U);
        phy_loopback_mode_en(1U);
        hear_pbus = phy_pbus_rd(1U, 1U);
        phy_pbus_force_test(1U, 1U, (hear_pbus | 2U) & 0xFFFFU);
        hear_i2c = phy_i2c_readReg_Mask(103U, 1U, 3U, 2U, 2U);
        phy_i2c_writeReg_Mask(103U, 1U, 3U, 2U, 2U, 0U);
        phy_set_loopback_gain(0U, 67U, 32U);
        phy_set_channel_dcode(6U);
        phy_pbus_force_test(1U, 1U, 0U);
        phy_pbus_force_test(1U, 1U, 505U);
        return;
    }
    phy_stop_tx_tone(1U);
    phy_i2c_writeReg_Mask(103U, 1U, 3U, 2U, 2U, hear_i2c);
    phy_pbus_force_test(1U, 1U, hear_pbus & 0xFFFFU);
    phy_loopback_mode_en(0U);
    phy_pbus_xpd_tx_off();
    phy_pbus_xpd_rx_on(1U);
    phy_pbus_workmode();
}

#endif /* TIKU_DRV_SDR_ESP_PROBE */
