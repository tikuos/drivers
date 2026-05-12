/*
 * Tiku Drivers - Skeleton driver implementation
 *
 * Template for new drivers. Demonstrates the descriptor pattern,
 * the init / deinit hooks, and a sample VFS contribution. Copy
 * the directory and rename `skeleton` everywhere.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_drv_skeleton.h"
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* DRIVER STATE                                                              */
/*---------------------------------------------------------------------------*/

/* Driver-local state. Keep it static and bounded — drivers run on
 * MCUs with kilobytes of SRAM, not megabytes. */
static struct {
    uint8_t  initialised;
    uint32_t sample_count;
} drv_state;

/*---------------------------------------------------------------------------*/
/* VFS HANDLERS                                                              */
/*---------------------------------------------------------------------------*/

/*
 * Read handler for /dev/<class>/<mount>/status. Returns a short
 * human-readable string. VFS handlers should write to `buf` and
 * return the number of bytes written (or -1 on error).
 */
static int
status_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n",
                    drv_state.initialised ? "ready" : "uninit");
}

static int
samples_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)drv_state.sample_count);
}

/* VFS node array. Splice point is /dev/<class>/<vfs_mount>/. */
static const tiku_vfs_node_t skeleton_vfs_nodes[] = {
    { "status",  TIKU_VFS_FILE, status_read,  NULL, NULL, 0 },
    { "samples", TIKU_VFS_FILE, samples_read, NULL, NULL, 0 },
};

/*---------------------------------------------------------------------------*/
/* LIFECYCLE                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * Called once at boot from tiku_drv_init_all(). Bring the
 * underlying hardware up here: peripheral reset release, clock
 * configuration, probe for chip-ID, etc. Return TIKU_DRV_OK on
 * success, a negative TIKU_DRV_ERR_* on failure.
 *
 * The kernel logs the result and continues to other drivers even
 * if this returns failure — a single broken driver should not
 * brick boot.
 */
static int
skeleton_init(void)
{
    /* TODO: real hardware bring-up goes here. Example sequence:
     *   1. Configure pins via arch GPIO / SIO API.
     *   2. Take peripheral out of reset.
     *   3. Verify chip-ID register matches expected value.
     *   4. Apply default configuration. */

    drv_state.initialised  = 1U;
    drv_state.sample_count = 0U;

    return TIKU_DRV_OK;
}

/*
 * Optional. Called when the kernel wants to release the driver
 * (rare on TikuOS — most drivers stay live for the lifetime of
 * the boot). Return TIKU_DRV_OK; non-zero if teardown failed.
 */
static int
skeleton_deinit(void)
{
    drv_state.initialised = 0U;
    return TIKU_DRV_OK;
}

/*---------------------------------------------------------------------------*/
/* DESCRIPTOR                                                                */
/*---------------------------------------------------------------------------*/

const tiku_drv_t tiku_drv_skeleton = {
    .name           = "skeleton",
    .class          = TIKU_DRV_CLASS_OTHER,
    .init           = skeleton_init,
    .deinit         = skeleton_deinit,
    .vfs_nodes      = skeleton_vfs_nodes,
    .vfs_node_count =
        sizeof(skeleton_vfs_nodes) / sizeof(skeleton_vfs_nodes[0]),
    .vfs_mount      = "skeleton0",
};
