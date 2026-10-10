/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_drv_phy_c5.h - explicit C5 PHY calibration, wake and RF shutdown.
 * This API does not start a Wi-Fi, Bluetooth or IEEE 802.15.4 MAC.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRV_PHY_C5_H_
#define TIKU_DRV_PHY_C5_H_
#include <stdint.h>

#define TIKU_C5_PHY_OK    0
#define TIKU_C5_PHY_BUSY  -1
#define TIKU_C5_PHY_CLOCK -2
#define TIKU_C5_PHY_FAULT -3

/** @brief Start the PHY, calibrating once per boot; return zero or a negative
 * error.
 * @note Kernel foreground with interrupts enabled. A calibration failure
 * requires reboot.
 */
int tiku_drv_phy_c5_on(void);
/** @brief Close RF and restore the saved digital clock fields; repeated off is
 * harmless.
 * @note Kernel foreground with interrupts enabled. Fault state retains analog
 * ownership.
 */
int tiku_drv_phy_c5_off(void);
/** @brief Return nonzero only after successful calibration or wake. */
int tiku_drv_phy_c5_active(void);
/** @brief Return successful full calibrations since boot. */
unsigned tiku_drv_phy_c5_calibrations(void);
/** @brief Return the latest full calibration duration in microseconds. */
uint32_t tiku_drv_phy_c5_calibration_us(void);
/** @brief Return the vendor calibration result, or -1 before a calibration. */
int tiku_drv_phy_c5_result(void);

#endif
