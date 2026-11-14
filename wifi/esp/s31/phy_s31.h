/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * phy_s31.h - Foreground S31 RF calibration and clock ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PHY_S31_H_
#define TIKU_PHY_S31_H_
#include <stdint.h>
/** @brief Calibrate or wake RF with a 10 dBm parameter ceiling; return 0 or -1.
 * Requires CPU0 kernel foreground with IRQs enabled and no other modem owner.
 * The vendor calibration call has no software timeout. Repeated on is refused.
 */
int tiku_drv_phy_s31_on(void);
/** @brief Close RF and restore acquired digital clock fields; return 0 or -1. */
int tiku_drv_phy_s31_off(void);
/** @brief Return whether this driver owns an initialized PHY. */
int tiku_drv_phy_s31_active(void);
/** @brief Return the last full-calibration duration in microseconds. */
uint32_t tiku_drv_phy_s31_calibration_us(void);
/** @brief Read the factory six-byte base MAC without modifying eFuses. */
int tiku_drv_phy_s31_mac(uint8_t mac[6]);
#endif
