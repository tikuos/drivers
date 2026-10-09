/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * phy_c5_glue.c - C5 libphy crystal query and bounded console formatting.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <hal/tiku_printf_hal.h>
#include <arch/esp32c5/tiku_esp32c5_regs.h>

/** @brief Read the crystal frequency recorded by C5 ROM startup, in MHz. */
uint32_t rtc_clk_xtal_freq_get(void)
{
    return (TIKU_C5_REG_READ(0x60096110u) >> 24) & 127u;
}

/** @brief Forward one vendor message through the TikuOS console. */
int phy_printf(const char *format, ...)
{
    char buffer[192];
    va_list args;
    int result;
    va_start(args, format);
    result = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    TIKU_PRINTF("%s", buffer);
    return result;
}
