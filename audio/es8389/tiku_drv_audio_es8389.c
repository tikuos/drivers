/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_audio_es8389.c - BCLK-derived ES8389 setup and muted shutdown.
 * Register sequences derive from Espressif esp-audio-dev; see README.md.
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */
#include "tiku_drv_audio_es8389.h"
#include <interfaces/bus/tiku_i2c_bus.h>
#include <hal/tiku_clock_hal.h>
#include <stddef.h>
#if !defined(PLATFORM_ESP32S31) && !defined(TIKU_HOST_TEST)
#error ES8389 register implementation is enabled for ESP32-S31
#endif
static unsigned opened, failed;
/** @brief Read one codec register through repeated START. */
static int read_reg(uint8_t reg, uint8_t *value)
{ return tiku_i2c_write_read(0x10, &reg, 1, value, 1) ? TIKU_DRV_ERR_IO : 0; }
/** @brief Write one codec register and retain bus failure. */
static int write_reg(uint8_t reg, uint8_t value)
{ uint8_t p[2] = {reg, value}; return tiku_i2c_write(0x10, p, 2) ? TIKU_DRV_ERR_IO : 0; }
/** @brief Write an ordered register/value sequence, stopping on the first error. */
static int sequence(const uint8_t (*pairs)[2], unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        if (write_reg(pairs[i][0], pairs[i][1])) return TIKU_DRV_ERR_IO;
    return 0;
}
int tiku_drv_audio_es8389_probe(void)
{
    uint8_t a, b;
    if (!tiku_i2c_get_config()) return TIKU_DRV_ERR_INIT;
    if (read_reg(0xfd, &a) || read_reg(0xfe, &b)) return TIKU_DRV_ERR_IO;
    return a == 0x83 && b == 0x89 ? 0 : TIKU_DRV_ERR_NOT_PRESENT;
}
int tiku_drv_audio_es8389_close(void)
{
    int result = 0;
    if (!opened) return 0;
    result |= write_reg(0x20, 0x63); result |= write_reg(0x40, 0x63);
    result |= write_reg(0x10, 0xd4);
    tiku_clock_arch_delay(70000);
    const uint8_t off[][2] = {{0x61,0x59},{0x64,0},{0x03,0},{0x00,0x7e},
        {0x69,0},{0x60,0},{0x00,0},{0x10,0xcc},{0xf2,0}};
    for (unsigned i = 0; i < sizeof off / sizeof *off; i++) result |= write_reg(off[i][0], off[i][1]);
    opened = failed = 0;
    return result ? TIKU_DRV_ERR_IO : 0;
}
int tiku_drv_audio_es8389_open(void)
{
    if (opened) return TIKU_DRV_ERR_INVALID;
    int result = tiku_drv_audio_es8389_probe();
    if (result) return result;
    opened = 1;
    const uint8_t init[][2] = {
        {0xf3,0},{0x00,0x7e},{0xf3,0x38},{0x24,0x64},{0x25,4},{0x45,3},
        {0x60,0x2a},{0x61,0xc9},{0x62,0x4f},{0x63,6},{0x6b,0},{0x6d,0x16},
        {0x6e,0xaa},{0x6f,0x66},{0x70,0x99},{0x23,0},{0x72,0x10},{0x73,0x10},
        {0x10,0xc4},{0x01,8},{0xf1,0},{0x12,1},{0x13,1},{0x14,1},{0x15,1},
        {0x16,0x12},{0x17,9},{0x18,0x31},{0x19,0x0e},{0x1a,1},{0x1b,1},{0x1c,0x11},
        {0x2a,0},{0x20,0x60},{0x40,0x63},{0xf0,0x12},{0x02,0x40},
        {0x04,0},{0x05,0x55},{0x06,0x84},{0x07,0xd0},{0x08,1},{0x09,0xc1},{0x0a,0x90},
        {0x0b,4},{0x0c,0},{0x0d,32},{0x0f,0x10},{0x11,0},
        {0x21,0x1f},{0x22,0x7f},{0x2f,0xc0},{0x30,0xf4},{0x31,0},{0x44,0},
        {0x41,0xff},{0x42,0x7f},{0x43,0x10},{0x49,0x0f},{0x4c,0xc0},
        {0x26,0xbf},{0x27,0xbf},{0x28,0xbf},{0x46,0x5f},{0x47,0x5f},{0x48,0xbe},
        {0x4d,0},{0x69,0x20},{0x61,0xd9},{0x64,0x8f},{0x10,0xe4},{0x00,1},
        {0x03,0xc3},{0x24,0x6a},{0x25,0x0a}
    };
    result = sequence(init, sizeof init / sizeof *init);
    const uint8_t verify[][2] = {{0x02,0x40},{0x05,0x55},{0x06,0x84},{0x20,0x60},{0x40,0x63},{0xf0,0x12}};
    for (unsigned i = 0; !result && i < sizeof verify / sizeof *verify; i++) {
        uint8_t value;
        if (read_reg(verify[i][0], &value) || value != verify[i][1]) result = TIKU_DRV_ERR_IO;
    }
    if (result) (void)tiku_drv_audio_es8389_close();
    return result;
}
int tiku_drv_audio_es8389_output(int attenuation_db, unsigned mute)
{
    if (!opened || failed || attenuation_db < -48 || attenuation_db > 0 || mute > 1)
        return TIKU_DRV_ERR_INVALID;
    uint8_t volume = (uint8_t)(191 + attenuation_db * 2);
    if (write_reg(0x40,0x63) || write_reg(0x46,volume) || write_reg(0x47,volume) ||
        write_reg(0x40,mute ? 0x63 : 0x60)) {
        failed = 1; (void)write_reg(0x40,0x63); return TIKU_DRV_ERR_IO;
    }
    return 0;
}
int tiku_drv_audio_es8389_input(int gain_db)
{
    /* PGA codes for 0, 3.5, 6.5, 9.5, then 12.5..36.5 dB in 3 dB steps. */
    static const uint8_t code[] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    if (!opened || failed || gain_db < 0 || gain_db > 36) return TIKU_DRV_ERR_INVALID;
    uint8_t value = (uint8_t)(0x10u | code[gain_db / 3]);
    return write_reg(0x72, value) || write_reg(0x73, value) ? TIKU_DRV_ERR_IO : 0;
}
const tiku_drv_t tiku_drv_audio_es8389 = {
    .name = "es8389", .class = TIKU_DRV_CLASS_OTHER,
    .init = tiku_drv_audio_es8389_probe, .deinit = tiku_drv_audio_es8389_close
};
