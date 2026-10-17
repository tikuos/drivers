/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_drv_sdr_tx_esp.h - C5/C61 preloaded IQ10 transmission at 80 MS/s.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRV_SDR_TX_ESP_H_
#define TIKU_DRV_SDR_TX_ESP_H_
#include <stdint.h>

#define TIKU_SDR_TX_HZ 80000000u
#define TIKU_SDR_TX_WORDS_MAX 16128u
#define TIKU_SDR_TX_ALIGN 256u
#define TIKU_SDR_TX_MS_MAX 1000u
#define TIKU_SDR_TX_CHUNK 8u
#define TIKU_SDR_TX_PARAM -1
#define TIKU_SDR_TX_BUSY -2
#define TIKU_SDR_TX_IO -3
#define TIKU_SDR_TX_FAULT -4
#define TIKU_SDR_TX_UNSUPPORTED -5
#define TIKU_SDR_TX_GUARD -6

/** @brief Inclusive carrier-center range with a step measured from low_mhz. */
typedef struct {
    uint32_t low_mhz, high_mhz, step_mhz;
} tiku_sdr_tx_band_t;

/** @brief Return the band count and optionally its stable read-only table. */
unsigned tiku_drv_sdr_tx_esp_bands(const tiku_sdr_tx_band_t **bands);

/** @brief Whether a carrier center belongs to an advertised TX channel grid. */
static inline int tiku_sdr_tx_frequency(uint32_t mhz)
{
    const tiku_sdr_tx_band_t *bands;
    unsigned count = tiku_drv_sdr_tx_esp_bands(&bands), n;
    for (n = 0; n < count; n++) {
        if (mhz >= bands[n].low_mhz && mhz <= bands[n].high_mhz &&
            (mhz - bands[n].low_mhz) % bands[n].step_mhz == 0) { return 1; }
    }
    return 0;
}

/** @brief Completion data for one bounded playback; levels are uncalibrated. */
typedef struct {
    uint32_t hz;             /**< Native playback rate. */
    uint32_t words;          /**< Complex samples repeated. */
    uint32_t elapsed_us;     /**< Playback interval including notification. */
    uint32_t play;           /**< PLAY readback while running. */
    uint32_t owner;          /**< SRAM owner readback while running. */
} tiku_sdr_tx_result_t;

/** @brief Reserve the SDR bank for a sequential upload without powering RF.
 * @note Kernel foreground with interrupts enabled; refuses an RX/PHY owner.
 * A successful begin replaces the previous TX waveform. Count is a multiple
 * of 256, from 256 through 16128. Invalid arguments preserve the waveform.
 */
int tiku_drv_sdr_tx_esp_begin(uint32_t words);
/** @brief Append packed IQ10 at the next sequential offset; validate the
 * entire chunk before writing. Upper twelve bits must be zero.
 * @note Foreground only. Accepted samples remain in SRAM until clear/reset.
 */
int tiku_drv_sdr_tx_esp_write(uint32_t offset, const uint32_t *samples,
                             uint32_t words);
/** @brief Load a 1024-sample sinusoid, -128..128 cycles, amplitude 1..128.
 * @note Signed cycles choose frequency direction; zero produces a carrier.
 */
int tiku_drv_sdr_tx_esp_tone(int cycles, unsigned amplitude);
/** @brief Repeat a complete waveform for 1..1000 ms, then stop the PHY.
 * @param mhz Carrier center on a grid returned by tiku_drv_sdr_tx_esp_bands().
 * @param started Optional foreground notification after PLAY starts; return
 * promptly. Its runtime counts toward the playback interval.
 * @note Calibration/preparation can emit RF and have no software timeout.
 * The waveform stays loaded for another burst; clear returns the bank.
 */
int tiku_drv_sdr_tx_esp_send(uint32_t mhz, uint32_t ms,
                            void (*started)(void *), void *context,
                            tiku_sdr_tx_result_t *result);
/** @brief Return the TX reservation; a PHY fault requires reset and retains it. */
int tiku_drv_sdr_tx_esp_clear(void);
/** @brief Whether TX holds the bank, including a partial upload or fault. */
int tiku_drv_sdr_tx_esp_held(void);
/** @brief Report sequential upload progress without touching RF registers. */
void tiku_drv_sdr_tx_esp_status(uint32_t *written, uint32_t *expected);
#endif
