/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_input_gt1151.c - Goodix identity and checksummed contact reports.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_drv_input_gt1151.h"
#include <interfaces/bus/tiku_i2c_bus.h>
#include <string.h>

#define ADDRESS 0x14u
#define PRODUCT 0x8140u
#define REPORT  0x814eu

/** @brief Read a big-endian register address through a repeated START. */
static int read_register(uint16_t reg, uint8_t *bytes, uint16_t count)
{
    uint8_t address[2] = {reg >> 8, reg & 255u};
    return tiku_i2c_write_read(ADDRESS, address, 2, bytes, count) == 0 ?
        TIKU_DRV_OK : TIKU_DRV_ERR_IO;
}

int tiku_drv_input_gt1151_identity(tiku_gt1151_identity_t *identity)
{
    uint8_t bytes[11];
    if (identity == NULL) return TIKU_DRV_ERR_INVALID;
    memset(identity, 0, sizeof(*identity));
    if (read_register(PRODUCT, bytes, sizeof(bytes)) != 0) return TIKU_DRV_ERR_IO;
    if ((memcmp(bytes, "1151", 4) && memcmp(bytes, "1158", 4)) || bytes[10] == 255u)
        return TIKU_DRV_ERR_NOT_PRESENT;
    memcpy(identity->product, bytes, 4);
    memcpy(identity->revision, bytes + 4, 7);
    return TIKU_DRV_OK;
}

/** @brief Decode an unaligned little-endian coordinate or contact strength. */
static uint16_t little16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int tiku_drv_input_gt1151_poll(tiku_gt1151_report_t *report)
{
    uint8_t bytes[1 + 8 * TIKU_GT1151_MAX_POINTS + 2], status;
    const uint8_t acknowledge[3] = {REPORT >> 8, REPORT & 255u, 0};
    if (report == NULL) return TIKU_DRV_ERR_INVALID;
    memset(report, 0, sizeof(*report));
    if (read_register(REPORT, &status, 1) != 0) return TIKU_DRV_ERR_IO;
    unsigned count = status & 15u;
    unsigned length = 1u + 8u * count + 2u;
    if (count && count <= TIKU_GT1151_MAX_POINTS &&
        read_register(REPORT, bytes, length) != 0) return TIKU_DRV_ERR_IO;
    if (tiku_i2c_write(ADDRESS, acknowledge, sizeof(acknowledge)) != 0)
        return TIKU_DRV_ERR_IO;
    if (count > TIKU_GT1151_MAX_POINTS) return TIKU_DRV_ERR_INVALID;
    if (!count) return TIKU_DRV_OK;
    uint8_t sum = 0;
    for (unsigned i = 0; i < length; i++) sum += bytes[i];
    if (sum || (bytes[0] & 15u) != count) return TIKU_DRV_ERR_INVALID;
    uint16_t seen = 0;
    for (unsigned i = 0; i < count; i++) {
        unsigned id = bytes[1 + i * 8] & 15u;
        if (seen & (1u << id)) return TIKU_DRV_ERR_INVALID;
        seen |= 1u << id;
    }
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *p = &bytes[1 + i * 8];
        report->points[i].id = p[0] & 15u;
        report->points[i].x = little16(p + 1);
        report->points[i].y = little16(p + 3);
        report->points[i].strength = little16(p + 5);
    }
    report->count = count;
    return TIKU_DRV_OK;
}

/** @brief Probe the fitted product without changing its configuration. */
static int initialize(void)
{
    tiku_gt1151_identity_t identity;
    return tiku_drv_input_gt1151_identity(&identity);
}

const tiku_drv_t tiku_drv_input_gt1151 = {
    .name = "gt1151",
    .class = TIKU_DRV_CLASS_INPUT,
    .init = initialize,
};
