/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * sdr_c5.h - C5 receive-only sample capture and exclusive PHY ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SDR_C5_H_
#define TIKU_SDR_C5_H_
#include <stdint.h>
#define C5_SDR_BANK_BASE 0x40820000UL
#define C5_SDR_BANK_BYTES 0x20000UL
#define C5_SDR_DATA_BASE 0x40830000UL
/** @brief Enable or disable an exclusively owned PHY; refuse another radio owner. */
int tiku_sdr_c5_power(int on);
/** @brief Whether the SDR receiver owns the PHY. */
int tiku_sdr_c5_active(void);
/** @brief Supported receive bands: 2400..2500 and 4900..5900 MHz. */
int tiku_sdr_c5_frequency(uint32_t mhz);
/** @brief Capture into the bank; return -1 invalid/busy, -2 timeout, -3 invalid data/guards. */
int tiku_sdr_c5_capture(uint32_t mhz, uint8_t rate, uint32_t words,
                        uint32_t *us, int gain);
#endif
