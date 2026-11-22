/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_input_gt1151.h - Polled Goodix GT1151/GT1158 touch reports.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRV_INPUT_GT1151_H_
#define TIKU_DRV_INPUT_GT1151_H_
#include <kernel/drivers/tiku_drv.h>

#define TIKU_GT1151_MAX_POINTS 10u
/** @brief One contact in the controller's untransformed coordinate system. */
typedef struct {
    uint16_t x, y, strength;
    uint8_t id;
} tiku_gt1151_point_t;
/**
 * @brief The controller's report.
 * @note fresh is 1 for a report the controller wrote since the last one
 * acknowledged; its count is the contacts on the panel, 0 when the last
 * finger has lifted. fresh is 0 when the controller has written none: count
 * is then 0 whatever touches the panel, and the contacts of the report before
 * stay in force.
 */
typedef struct {
    uint8_t count, fresh;
    tiku_gt1151_point_t points[TIKU_GT1151_MAX_POINTS];
} tiku_gt1151_report_t;
/** @brief Product bytes, patch, mask and sensor revision read from 0x8140. */
typedef struct {
    char product[5];
    uint8_t revision[7];
} tiku_gt1151_identity_t;

/** @brief Read and validate a GT1151/GT1158 identity at seven-bit address 0x14.
 * @return TIKU_DRV_OK or a negative driver error; output is zero on failure.
 * @note Requires an initialized I2C bus. Calls must be serialized in scheduler
 * context with other bus users. The driver does not reset or configure the bus.
 */
int tiku_drv_input_gt1151_identity(tiku_gt1151_identity_t *identity);
/** @brief Read, validate and acknowledge a touch report at address 0x14.
 * @return TIKU_DRV_OK or a negative driver error; output is zero on failure.
 * @note A poll that finds no new report reads the status register, writes
 * nothing and returns TIKU_DRV_OK with fresh 0. Coordinates are raw; display
 * rotation and bounds belong to the caller. Invalid complete reports are
 * acknowledged and rejected. Failed reads are left unacknowledged. Calls
 * obey the identity function's bus requirements.
 */
int tiku_drv_input_gt1151_poll(tiku_gt1151_report_t *report);
/** @brief Driver descriptor; initialization probes the shared, open I2C bus. */
extern const tiku_drv_t tiku_drv_input_gt1151;
#endif
