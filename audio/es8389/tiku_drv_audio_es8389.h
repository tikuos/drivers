/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_audio_es8389.h - ES8389 stereo PCM16 control on an open I2C bus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRV_AUDIO_ES8389_H_
#define TIKU_DRV_AUDIO_ES8389_H_
#include <kernel/drivers/tiku_drv.h>
/** @brief Identify the codec at seven-bit address 0x10 without configuring it. */
int tiku_drv_audio_es8389_probe(void);
/** @brief Configure stereo I2S PCM16, 16 kHz, 512 kHz BCLK input and muted DAC.
 * @note Serialized foreground calls on an open I2C bus; BCLK/WS must run.
 * Caller owns clocks and amplifier sequencing. Failed opens attempt shutdown.
 */
int tiku_drv_audio_es8389_open(void);
/** @brief Set both DAC channels to -48..0 dB and apply mute; errors latch failure. */
int tiku_drv_audio_es8389_output(int attenuation_db, unsigned mute);
/** @brief Set both differential microphone inputs' PGA to the codec step at
 *  @p gain_db rounded down to a multiple of 3, 0..36; the steps above 0 dB
 *  are 0.5 dB higher.  A failed write returns TIKU_DRV_ERR_IO. */
int tiku_drv_audio_es8389_input(int gain_db);
/** @brief Mute ADC/DAC and power down the codec; keep the shared I2C bus open. */
int tiku_drv_audio_es8389_close(void);
/** @brief Probe-only registration; streaming setup is explicit. */
extern const tiku_drv_t tiku_drv_audio_es8389;
#endif
