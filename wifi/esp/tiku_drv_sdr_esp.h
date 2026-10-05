/*
 * Tiku Drivers - ESP32-C61 radio as a receiver: raw I/Q snapshots
 *
 * The PHY's own ADC samples, written by the modem's dump unit into one 64 KB
 * SRAM bank the CPU lends it for the capture; complex baseband around the
 * tuned frequency, 10 bits each way.  Receive only, and the radio must be
 * up (the Wi-Fi driver brings the PHY up) and otherwise quiet.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_SDR_ESP_H_
#define TIKU_DRV_SDR_ESP_H_

#include <stdint.h>

#define TIKU_DRV_SDR_ESP_WORDS_MAX  16380U      /* one bank, less a guard */
#define TIKU_DRV_SDR_ESP_RATES      6U          /* 80 40 20 10 8 4 MS/s */
#define TIKU_DRV_SDR_ESP_BINS       32U

/* The gain index captures are held at unless told: about a decibel a step,
 * the AGC idling at 76, and 60 the lowest whose noise floor is still the
 * antenna's rather than the ADC's -- as sensitive, 16 dB further from the
 * rail.  Held, the levels hold still while the AGC would chase each burst. */
#define TIKU_DRV_SDR_ESP_HOLD       60

/** @brief What one snapshot measured. */
typedef struct {
    uint32_t words;         /**< samples captured */
    uint32_t us;            /**< the capture's wall time */
    uint32_t hz;            /**< the sample rate */
    uint8_t  gain;          /**< the receiver's gain index, first sample */
    uint8_t  loud;          /**< the loudest slice: the spectrum's window */
    int16_t  dc_i, dc_q;    /**< mean I and Q */
    uint16_t rms;           /**< root mean square of |x| */
    uint16_t peak;          /**< largest |I| or |Q| */
    uint32_t slice[16];     /**< mean power, sixteenths of the capture */
    uint32_t bin[TIKU_DRV_SDR_ESP_BINS]; /**< power by frequency in the loudest
                                              slice, low to high */
} tiku_drv_sdr_esp_result_t;

/** @brief Lend the capture bank from the SRAM tier, before the radio takes
 *         its heap.  @return 0, or -1 (said why) */
int tiku_drv_sdr_esp_reserve(void);

/** @brief Give the bank back. */
void tiku_drv_sdr_esp_release(void);

/** @brief The sample rate rate code @p rate selects, in Hz; 0 if none. */
uint32_t tiku_drv_sdr_esp_rate_hz(uint8_t rate);

/**
 * @brief One snapshot: tune to @p mhz, capture @p words at rate code @p rate,
 *        then measure it into @p out.  Interrupts are off meanwhile.
 *
 * @return 0, -1 without the bank or the radio, -2 when the capture did not
 *         complete, -3 when the unit wrote nothing
 */
int tiku_drv_sdr_esp_capture(uint32_t mhz, uint8_t rate, uint32_t words,
                             tiku_drv_sdr_esp_result_t *out);

/**
 * @brief A spectrum: one capture of @p nfft x 32 samples, each block of @p
 *        nfft windowed (Hann) and transformed, the power averaged.
 *
 * @param db    @p nfft bins, low to high frequency, in half-decibels of an
 *              uncalibrated scale (the receiver's gain sets the level)
 * @param gain  the receiver's gain index during the capture: the one held
 *              (taken lower again while the ADC clips), or the AGC's at the
 *              first sample
 * @return 0, or as tiku_drv_sdr_esp_capture() fails; nfft 64, 128 or 256
 */
int tiku_drv_sdr_esp_spectrum(uint32_t mhz, uint8_t rate, unsigned nfft,
                              uint8_t *db, uint8_t *gain);

/** @brief Whether the capture bank is lent. */
int tiku_drv_sdr_esp_reserved(void);

/** @brief Hold the receiver's gain at index @p index (0-255) for the
 *         captures that follow, or -1: leave it to the AGC. */
void tiku_drv_sdr_esp_hold(int index);

/** @brief The gain index held, or -1 while the AGC sets it. */
int tiku_drv_sdr_esp_held(void);

/** @brief The last snapshot's words: I in bits 0-9, Q in 10-19, both two's
 *         complement, the gain index above; Q is negated against the air
 *         (conjugate before a transform).  NULL without the bank. */
const uint32_t *tiku_drv_sdr_esp_samples(void);

#if TIKU_DRV_SDR_ESP_PROBE
/* The transmit side, as found: lab probes (TIKU_DRV_SDR_ESP_PROBE=1). */

/** @brief The transmit side's self-test: the chain forced on with @p xpd
 *         (two analog-bus words), gain index @p pwr, tone generator 1 at
 *         @p step with @p gain; 0 stops it all.  @return libphy's word */
int tiku_drv_sdr_esp_tone(int on, unsigned xpd_a, unsigned xpd_b,
                          unsigned pwr, unsigned step, int gain);

/** @brief The chip's internal transmit-to-receive loopback, as the transmit
 *         calibration uses it: on with gain words @p a, @p b, @p c, or off. */
void tiku_drv_sdr_esp_loopback(int on, unsigned a, unsigned b, unsigned c);

/** @brief libphy's own transmit test mode, entered (1) or left (0). */
void tiku_drv_sdr_esp_txcal(int on);

/** @brief The dump unit's tap for the next captures: 15 is the ADC; the
 *         others are the exploration's. */
void tiku_drv_sdr_esp_source(uint8_t source);

/** @brief libphy's power detector: the tone's power (@p tone non-zero,
 *         started and stopped by the library) or what is there now. */
int tiku_drv_sdr_esp_power_db(int tone, unsigned gain, unsigned sel);

/** @brief Tone generator 1 alone, started at @p step with @p gain, or
 *         stopped: libphy's word. */
int tiku_drv_sdr_esp_nco(int on, unsigned step, int gain);

/** @brief A snapshot while the CPU toggles tone 1's step between @p a and
 *         @p b every @p half core cycles: the chip's own FSK, as received. */
int tiku_drv_sdr_esp_fsk(uint32_t mhz, uint8_t rate, uint32_t words,
                         unsigned a, unsigned b, uint32_t half,
                         tiku_drv_sdr_esp_result_t *out);

/** @brief The receive calibration's own way of hearing the chip's tone:
 *         loopback with the analog bus set as phy_rxiq_cal_init sets it. */
void tiku_drv_sdr_esp_hear(int on);

#endif /* TIKU_DRV_SDR_ESP_PROBE */

#endif /* TIKU_DRV_SDR_ESP_H_ */
