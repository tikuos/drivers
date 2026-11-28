/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_input_gt911.h - polled Goodix GT911 contact reports.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRV_INPUT_GT911_H_
#define TIKU_DRV_INPUT_GT911_H_
#include <interfaces/input/tiku_touch.h>
#include <kernel/drivers/tiku_drv.h>

/** @brief Find a GT911 at its strapped address on an open I2C bus.
 * @return Zero on identification or a driver error. A failed probe clears the address. */
int tiku_drv_input_gt911_probe(void);
/** @brief Poll a probed GT911. A fresh zero-contact report releases the pointer.
 * @return Zero on success. A bus or malformed-report error sets cancel. */
int tiku_drv_input_gt911_poll(tiku_touch_report_t *out);
extern const tiku_drv_t tiku_drv_input_gt911;
#endif
