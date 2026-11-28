/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_input_gt911.c - GT911 identification and complete contact reports.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_drv_input_gt911.h"
#include <interfaces/bus/tiku_i2c_bus.h>
#include <string.h>

#define PRODUCT 0x8140u
#define STATUS  0x814eu
#define POINTS  5u
static uint8_t address;

static int read_reg(uint8_t addr, unsigned reg, uint8_t *dst, unsigned len)
{
    const uint8_t prefix[] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return tiku_i2c_write_read(addr, prefix, 2, dst, len);
}

int tiku_drv_input_gt911_probe(void)
{
    const uint8_t straps[] = {0x5d, 0x14};
    uint8_t id[4];
    address = 0;
    int result = TIKU_DRV_ERR_NOT_PRESENT;
    for (unsigned i = 0; i < sizeof straps; ++i) {
        int rc = read_reg(straps[i], PRODUCT, id, sizeof id);
        if (rc == TIKU_I2C_OK && !memcmp(id, "911", 4)) {
            address = straps[i];
            return TIKU_DRV_OK;
        }
        if (rc != TIKU_I2C_OK && rc != TIKU_I2C_ERR_NACK) result = TIKU_DRV_ERR_IO;
    }
    return result;
}

int tiku_drv_input_gt911_poll(tiku_touch_report_t *out)
{
    uint8_t status, data[POINTS * 8];
    const uint8_t ack[] = {STATUS >> 8, STATUS & 255, 0};
    if (!out) return TIKU_DRV_ERR_INVALID;
    *out = (tiku_touch_report_t){.cancel = 1};
    if (!address) return TIKU_DRV_ERR_NOT_PRESENT;
    if (read_reg(address, STATUS, &status, 1)) return TIKU_DRV_ERR_IO;
    if (!(status & 0x80)) {
        out->cancel = 0;
        return TIKU_DRV_OK;
    }
    unsigned n = status & 15u;
    if (n && n <= POINTS && read_reg(address, STATUS + 1, data, 8 * n))
        return TIKU_DRV_ERR_IO;
    if (tiku_i2c_write(address, ack, sizeof ack)) return TIKU_DRV_ERR_IO;
    if (n > POINTS) return TIKU_DRV_ERR_INVALID;
    unsigned used = 0;
    tiku_touch_report_t report = {.fresh = 1, .count = n};
    for (unsigned i = 0; i < n; ++i) {
        const uint8_t *p = &data[i * 8];
        unsigned id = p[0];
        if (id >= 16 || (used & (1u << id))) return TIKU_DRV_ERR_INVALID;
        used |= 1u << id;
        report.contacts[i].id = id;
        report.contacts[i].x = p[1] | ((unsigned)p[2] << 8);
        report.contacts[i].y = p[3] | ((unsigned)p[4] << 8);
    }
    *out = report;
    return TIKU_DRV_OK;
}

const tiku_drv_t tiku_drv_input_gt911 = {
    .name = "gt911",
    .class = TIKU_DRV_CLASS_INPUT,
    .init = tiku_drv_input_gt911_probe
};
