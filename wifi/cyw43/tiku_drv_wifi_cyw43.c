/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_wifi_cyw43.c - CYW43439 WiFi driver, phase 0
 *
 * Phase 0 deliverable: prove the driver registry picks up this
 * driver, init() runs, and the boot output reflects it. No real
 * radio activity. Subsequent phases (gSPI transport, firmware
 * upload, WHD protocol, association, link adapter, VFS nodes)
 * fill in init() and the rest of this file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_drv_wifi_cyw43.h"
#include "gspi.h"
#include "whd.h"
#include "tiku.h"

#ifndef CYW43_PRINTF
#define CYW43_PRINTF(...) TIKU_PRINTF("[cyw43] " __VA_ARGS__)
#endif

/*---------------------------------------------------------------------------*/
/* DRIVER STATE                                                              */
/*---------------------------------------------------------------------------*/

static struct {
    uint8_t  shell_loaded;     /* phase 0: prove the shell ran */
    /* phase 1+ fields land here (gspi_initialised, fw_loaded,
     * link_up, assoc_state, etc.) */
} drv;

/*---------------------------------------------------------------------------*/
/* LIFECYCLE                                                                 */
/*---------------------------------------------------------------------------*/

static int
cyw43_init(void)
{
    /*
     * Phase 0: just announce that the registry plumbing reached
     * this code. Everything from gSPI bring-up onward is the next
     * phase; do NOT touch any CYW43 silicon yet — the WL_REG_ON
     * pin is unpowered and the chip is in its boot state.
     */
    drv.shell_loaded = 1U;
    CYW43_PRINTF("driver shell loaded\n");

    /* Phase 1: bring up the gSPI transport and probe the F0
     * SPI_TEST_RO register. Init failures are logged but don't fail
     * the whole driver — power-on succeeded even if the rest of the bus
     * isn't talking yet, which is useful info for the bench.
     *
     * Once phase 1 is verified on hardware, this is also where
     * phase 2 (firmware upload) and phase 3 (WHD init) plug in. */
    if (cyw43_gspi_init() != TIKU_DRV_OK) {
        CYW43_PRINTF("gspi init failed — bus not ready\n");
        return TIKU_DRV_OK; /* shell still loaded; surface to bench */
    }

    if (cyw43_gspi_probe_chip_id() != TIKU_DRV_OK) {
        CYW43_PRINTF("gSPI bus probe failed (phase 1 pending)\n");
        return TIKU_DRV_OK;
    }

    /* Probe brought firmware up + HT clock. Hand off to the WHD
     * runner process — it owns the SDPCM/CDC bring-up and the
     * event-driven loop above it. The runner runs whd_bring_up()
     * synchronously on its first dispatch, then sits waiting for
     * CYW43_WIFI_EVT_* events. */
    if (whd_runner_init() != TIKU_DRV_OK) {
        CYW43_PRINTF("WHD runner init failed\n");
        return TIKU_DRV_OK;
    }
    return TIKU_DRV_OK;
}

static int
cyw43_deinit(void)
{
    /* No hardware to release in phase 0. */
    drv.shell_loaded = 0U;
    return TIKU_DRV_OK;
}

/*---------------------------------------------------------------------------*/
/* DESCRIPTOR                                                                */
/*---------------------------------------------------------------------------*/

/*
 * vfs_nodes is empty for phase 0. Phase 6 will populate the
 * array with ssid / rssi / status / scan handlers. The kernel's
 * runtime VFS-mount hook is still TODO either way, so a non-empty
 * array here would not be observable yet.
 */
const tiku_drv_t tiku_drv_wifi_cyw43 = {
    .name           = "wifi-cyw43",
    .class          = TIKU_DRV_CLASS_WIFI,
    .init           = cyw43_init,
    .deinit         = cyw43_deinit,
    .vfs_nodes      = (const tiku_vfs_node_t *)0,
    .vfs_node_count = 0U,
    .vfs_mount      = "wifi0",
};
