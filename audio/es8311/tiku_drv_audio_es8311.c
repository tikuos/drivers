/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_audio_es8311.c - ES8311 mono ADC, 256 fs MCLK and PCM16 output.
 * Register settings follow the ES8311 datasheet and Espressif esp-bsp ES8311 driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_drv_audio_es8311.h"
#include <interfaces/bus/tiku_i2c_bus.h>
#include <hal/tiku_common_hal.h>

#define ADDRESS 0x18u
static int opened;

static int write_reg(uint8_t reg, uint8_t value)
{
    uint8_t pair[2] = {reg, value};
    return tiku_i2c_write(ADDRESS, pair, sizeof pair) ? TIKU_DRV_ERR_IO : 0;
}

static int probe(void)
{
    uint8_t id[2];
    for (unsigned i = 0; i < 2; i++) {
        uint8_t reg = 0xfdu + i;
        if (tiku_i2c_write_read(ADDRESS, &reg, 1, &id[i], 1)) return TIKU_DRV_ERR_IO;
    }
    return id[0] == 0x83 && id[1] == 0x11 ? 0 : TIKU_DRV_ERR_NOT_PRESENT;
}

int tiku_drv_audio_es8311_close(void)
{
    if (!opened) return 0;
    int rc = write_reg(0x0a, 0x4c); /* mute serial output */
    rc |= write_reg(0x0e, 0xff);
    rc |= write_reg(0x0d, 0xfc);
    rc |= write_reg(0x01, 0);
    opened = 0;
    return rc ? TIKU_DRV_ERR_IO : 0;
}

int tiku_drv_audio_es8311_open(void)
{
    static const uint8_t config[][2] = {
        {0x00, 0}, {0x00, 0x80}, /* power up in slave mode */
        {0x01, 0x3f}, {0x02, 0}, {0x03, 0x10}, {0x04, 0x10},
        {0x05, 0}, {0x06, 3}, {0x07, 0}, {0x08, 0xff}, /* 256 fs */
        {0x09, 0x4c}, {0x0a, 0x0c}, /* muted DAC input, 16-bit ADC output */
        {0x0d, 1}, {0x0e, 2}, /* analog supply, PGA and ADC modulator */
        {0x16, 4}, {0x1c, 0x6a} /* nominal ADC scale, high-pass filter */
    };
    if (opened) return TIKU_DRV_ERR_INVALID;
    int rc = probe();
    if (rc) return rc;
    opened = 1;
    rc = write_reg(0, 0x1f);
    if (!rc) tiku_common_arch_delay_ms(20);
    for (unsigned i = 0; !rc && i < sizeof config / sizeof config[0]; i++)
        rc = write_reg(config[i][0], config[i][1]);
    if (rc) (void)tiku_drv_audio_es8311_close();
    return rc;
}

int tiku_drv_audio_es8311_gain(int db)
{
    if (!opened || db < 0 || db > 36) return TIKU_DRV_ERR_INVALID;
    int pga = db < 30 ? db / 3 : 10;
    /* The analog PGA covers 0 to 30 dB in 3 dB steps. */
    if (write_reg(0x14, (uint8_t)(0x10 | pga))) return TIKU_DRV_ERR_IO;
    return write_reg(0x17, (uint8_t)(0xbf + 2 * (db - 3 * pga)));
}

const tiku_drv_t tiku_drv_audio_es8311 = {
    .name = "es8311", .class = TIKU_DRV_CLASS_OTHER,
    .init = probe, .deinit = tiku_drv_audio_es8311_close
};
