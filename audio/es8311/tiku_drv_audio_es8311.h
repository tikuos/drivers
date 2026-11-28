/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_audio_es8311.h - ES8311 analog microphone on a shared I2C bus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRV_AUDIO_ES8311_H_
#define TIKU_DRV_AUDIO_ES8311_H_
#include <kernel/drivers/tiku_drv.h>

/** @brief Configure the ADC for 16 kHz Philips I2S with a running 4.096 MHz MCLK. */
int tiku_drv_audio_es8311_open(void);
/** @brief Set ADC gain to 0 through 36 dB. */
int tiku_drv_audio_es8311_gain(int db);
/** @brief Power down the ADC, leaving the shared I2C bus open. */
int tiku_drv_audio_es8311_close(void);
extern const tiku_drv_t tiku_drv_audio_es8311;
#endif
