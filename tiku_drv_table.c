/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * tiku_drv_table.c - Driver descriptor table
 *
 * Hand-maintained list of every driver this repo can produce.
 * Each entry is gated by its own TIKU_DRV_<CLASS>_<NAME>_ENABLE
 * flag, defined by that driver's build.mk when the user passes
 * the flag on the make command line. Drivers not enabled
 * contribute zero bytes to the final image.
 *
 * Adding a driver:
 *   1. Create tikudrivers/<class>/<name>/ from skeleton/.
 *   2. Add an `extern const tiku_drv_t tiku_drv_<class>_<name>;`
 *      below, guarded by the enable flag.
 *   3. Add an `&tiku_drv_<class>_<name>,` entry to the array
 *      below, same guard.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/drivers/tiku_drv.h"

/*---------------------------------------------------------------------------*/
/* DRIVER EXTERNS                                                            */
/*---------------------------------------------------------------------------*/

#if TIKU_DRV_WIFI_CYW43_ENABLE
extern const tiku_drv_t tiku_drv_wifi_cyw43;
#endif

#if TIKU_DRV_SENSOR_TEMPERATURE_MCP9808_ENABLE
extern const tiku_drv_t tiku_drv_sensor_temperature_mcp9808;
#endif

/* Add more `extern` declarations above, then the corresponding
 * array entry below. Keep them sorted by class then name so the
 * table is easy to scan. */

/*---------------------------------------------------------------------------*/
/* DRIVER TABLE                                                              */
/*---------------------------------------------------------------------------*/

const tiku_drv_t *const tiku_drv_table[] = {
#if TIKU_DRV_WIFI_CYW43_ENABLE
    &tiku_drv_wifi_cyw43,
#endif

#if TIKU_DRV_SENSOR_TEMPERATURE_MCP9808_ENABLE
    &tiku_drv_sensor_temperature_mcp9808,
#endif

    /* Add new entries above. The trailing NULL handles the
     * zero-driver case cleanly without requiring a length-0
     * array (which is a GNU extension). */
    (const tiku_drv_t *)0,
};

const uint8_t tiku_drv_table_count =
    (uint8_t)((sizeof(tiku_drv_table) / sizeof(tiku_drv_table[0])) - 1U);
