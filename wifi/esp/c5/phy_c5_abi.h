/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * phy_c5_abi.h - C5 libphy signatures from ESP-IDF 4d59230 esp_phy_init.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRV_PHY_C5_ABI_H_
#define TIKU_DRV_PHY_C5_ABI_H_
#include <stdint.h>

typedef struct { uint8_t params[256]; } c5_phy_init_t;
typedef struct {
    uint8_t version[4];
    uint8_t mac[6];
    uint8_t opaque[1894];
} c5_phy_calibration_t;

_Static_assert(sizeof(c5_phy_init_t) == 256, "C5 PHY initialization ABI");
_Static_assert(sizeof(c5_phy_calibration_t) == 1904, "C5 PHY calibration ABI");

/** @brief Calibrate the C5 PHY; mode 2 requests a full calibration. */
int register_chipv7_phy(const c5_phy_init_t *init, c5_phy_calibration_t *cal, int mode);
/** @brief Restore the PHY's retained RF calibration. */
void phy_wakeup_init(void);
/** @brief Close RF activity before removing its digital clocks. */
void phy_close_rf(void);
/** @brief Disable the PHY temperature sensor. */
void phy_xpd_tsens(void);
/** @brief Wait for the PHY frequency-hopping state machine to finish. */
void phy_wait_freq_hw_hop_done(void);
/** @brief Return the PHY library's version string. */
const char *get_phy_version_str(void);

#endif
