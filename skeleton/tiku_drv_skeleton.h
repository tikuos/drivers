/*
 * Tiku Drivers - Skeleton driver header
 *
 * Copy this directory to start a new driver. Replace every
 * occurrence of `skeleton` / `SKELETON` with the concrete driver
 * name. See ../README.md for the full checklist.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_SKELETON_H_
#define TIKU_DRV_SKELETON_H_

#include "kernel/drivers/tiku_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The descriptor is the kernel's handle on this driver. It is
 * defined in tiku_drv_skeleton.c and referenced by an extern in
 * drivers/tiku_drv_table.c (guarded by TIKU_DRV_SKELETON_ENABLE).
 */
extern const tiku_drv_t tiku_drv_skeleton;

/*
 * Any public driver-specific API goes here. Most drivers only
 * need the descriptor — VFS reads/writes are how application code
 * normally interacts. Expose a function-call API only when a VFS
 * round-trip would be inappropriate (e.g. very fast hot paths,
 * binary-protocol handles).
 *
 * Example:
 *
 *   int tiku_drv_skeleton_read_temperature(int16_t *out_celsius_x10);
 */

#ifdef __cplusplus
}
#endif

#endif /* TIKU_DRV_SKELETON_H_ */
