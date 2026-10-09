/*
 * Tiku Drivers - ESP32-C61 radio libraries, what they expect around them
 *
 * The symbols left undefined once the libraries meet the ROM's scripts:
 * IDF's print hooks, tables and small helpers, and data the ECO4 ROM keeps
 * itself.  Mesh, ESP-NOW and FTM are not used, so theirs are inert.  Shared
 * by both radios; what one does not use the link drops.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include <hal/tiku_printf_hal.h>
#include "esp_abi.h"
#if defined(PLATFORM_ESP32C5)
#include <arch/esp32c5/tiku_esp32c5_regs.h>
#endif

/* Event base name the stack posts its events under. */
const char *WIFI_EVENT = "WIFI_EVENT";

/* The libraries print through these: formatted here, since the console's
 * own printf takes no va_list and fewer conversions. */
static int esp_vlog(const char *fmt, va_list ap) {
    char line[128];
    int n = vsnprintf(line, sizeof line, fmt, ap);

    TIKU_PRINTF("%s", line);
    return n;
}

int pp_printf(const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = esp_vlog(fmt, ap);
    va_end(ap);
    return n;
}

int net80211_printf(const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = esp_vlog(fmt, ap);
    va_end(ap);
    return n;
}

int phy_printf(const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = esp_vlog(fmt, ap);
    va_end(ap);
    return n;
}

/* Their stdio: straight to the console, not newlib's, whose files go
 * nowhere here (the BLE controller reports its errors with printf). */
int printf(const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = esp_vlog(fmt, ap);
    va_end(ap);
    return n;
}

int putchar(int c) {
    TIKU_PRINTF("%c", c);
    return c;
}

int puts(const char *s) {
    TIKU_PRINTF("%s\n", s);
    return 0;
}

/* An assertion or an abort in a library is fatal: say so, then fault, so
 * the kernel's dump shows where. */
void __assert_func(const char *file, int line, const char *func,
                   const char *expr) {
    TIKU_PRINTF("[esp-wifi] assert %s at %s:%d (%s)\n", expr, file, line,
                func != NULL ? func : "?");
    __builtin_trap();
}

void abort(void) {
    TIKU_PRINTF("[esp-wifi] abort from 0x%08lx\n",
                (unsigned long)(uintptr_t)__builtin_return_address(0));
    __builtin_trap();
}

/** @brief The crystal, in MHz: the DevKitC's is 40. */
uint32_t rtc_clk_xtal_freq_get(void) {
#if defined(PLATFORM_ESP32C5)
    return (TIKU_C5_REG_READ(0x60096110u) >> 24) & 127u;
#else
    return 40U;
#endif
}

/** @brief Hex text to bytes, two digits each; -1 on a stray character. */
int hexstr2bin(const char *hex, uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        int v = 0;

        for (int k = 0; k < 2; k++) {
            char c = hex[2 * i + (size_t)k];

            v <<= 4;
            if (c >= '0' && c <= '9') {
                v |= c - '0';
            } else if (c >= 'a' && c <= 'f') {
                v |= c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                v |= c - 'A' + 10;
            } else {
                return -1;
            }
        }
        buf[i] = (uint8_t)v;
    }
    return 0;
}

/* The ECO4 ROM keeps this pointer itself; on ECO3 it lives here. */
void *s_offchan_tx_progress_in_ptr;
#if defined(PLATFORM_ESP32C5)
/* C5 ECO3's PHY dispatch pointer is supplied by the application on ECO2. */
void *s_phy_get_max_pwr_new_ptr;
#endif

/* ESP-NOW's vendor OUI, and mesh's auth timer: neither is used. */
uint8_t g_espnow_user_oui[3] = { 0x18, 0xFE, 0x34 };

int mesh_sta_auth_expire_time(void) {
    return 0;
}

/* The countries the stack may be told it is in, and their 2.4 GHz rules:
 * channels, widest band (2 = 40 MHz, 1 = 20 MHz) and power cap in dBm.
 * "01" is the world-safe default it starts in. */
#define RULE(first, last, bw, dbm) { (first), (last), (bw), (dbm), 0, 0 }

#if !defined(PLATFORM_ESP32C5)
const wifi_regulatory_t regulatory_data[] = {
    { 1, { RULE(1, 11, 2, 20) } },                      /* 0: world */
    { 1, { RULE(1, 13, 2, 20) } },                      /* 1: Europe, CN */
    { 1, { RULE(1, 11, 2, 30) } },                      /* 2: US */
    { 1, { RULE(1, 13, 2, 23) } },                      /* 3: SG */
    { 2, { RULE(1, 13, 2, 20), RULE(14, 14, 1, 20) } }, /* 4: JP */
    { 1, { RULE(1, 13, 2, 36) } },                      /* 5: AU */
    { 1, { RULE(1, 13, 2, 30) } },                      /* 6: IN */
};

const wifi_regdomain_t regdomain_table[] = {
    { { '0', '1' }, 0 }, { { 'E', 'U' }, 1 }, { { 'G', 'B' }, 1 },
    { { 'D', 'E' }, 1 }, { { 'C', 'N' }, 1 }, { { 'U', 'S' }, 2 },
    { { 'S', 'G' }, 3 }, { { 'J', 'P' }, 4 }, { { 'A', 'U' }, 5 },
    { { 'I', 'N' }, 6 },
    { { '#', '#' }, sizeof regulatory_data / sizeof regulatory_data[0] },
};
#endif

/* FTM's per-bandwidth delay compensation, unused without FTM. */
#define FTM_COMP(name) const int32_t name = 0
#if defined(PLATFORM_ESP32C5)
FTM_COMP(est_PHY_INIT_FTM_COMP_20_20_MHZ_5G);
FTM_COMP(est_PHY_INIT_FTM_COMP_20_20_MHZ_5G_DIS);
FTM_COMP(est_PHY_INIT_FTM_COMP_20_40_MHZ_5G);
FTM_COMP(est_PHY_INIT_FTM_COMP_20_40_MHZ_5G_DIS);
FTM_COMP(est_PHY_INIT_FTM_COMP_40_40_MHZ_5G);
FTM_COMP(est_PHY_INIT_FTM_COMP_40_40_MHZ_5G_DIS);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_20_MHZ_5G);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_20_MHZ_5G_DIS);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_40_MHZ_5G);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_40_MHZ_5G_DIS);
FTM_COMP(est_PHY_RESP_FTM_COMP_40_40_MHZ_5G);
FTM_COMP(est_PHY_RESP_FTM_COMP_40_40_MHZ_5G_DIS);
#endif
FTM_COMP(est_PHY_INIT_FTM_COMP_20_20D_MHZ);
FTM_COMP(est_PHY_INIT_FTM_COMP_20_20D_MHZ_DIS);
FTM_COMP(est_PHY_INIT_FTM_COMP_20_20U_MHZ);
FTM_COMP(est_PHY_INIT_FTM_COMP_20_20U_MHZ_DIS);
FTM_COMP(est_PHY_INIT_FTM_COMP_20_40D_MHZ);
FTM_COMP(est_PHY_INIT_FTM_COMP_20_40D_MHZ_DIS);
FTM_COMP(est_PHY_INIT_FTM_COMP_20_40U_MHZ);
FTM_COMP(est_PHY_INIT_FTM_COMP_20_40U_MHZ_DIS);
FTM_COMP(est_PHY_INIT_FTM_COMP_40_40D_MHZ);
FTM_COMP(est_PHY_INIT_FTM_COMP_40_40D_MHZ_DIS);
FTM_COMP(est_PHY_INIT_FTM_COMP_40_40U_MHZ);
FTM_COMP(est_PHY_INIT_FTM_COMP_40_40U_MHZ_DIS);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_20D_MHZ);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_20D_MHZ_DIS);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_20U_MHZ);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_20U_MHZ_DIS);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_40D_MHZ);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_40D_MHZ_DIS);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_40U_MHZ);
FTM_COMP(est_PHY_RESP_FTM_COMP_20_40U_MHZ_DIS);
FTM_COMP(est_PHY_RESP_FTM_COMP_40_40D_MHZ);
FTM_COMP(est_PHY_RESP_FTM_COMP_40_40D_MHZ_DIS);
FTM_COMP(est_PHY_RESP_FTM_COMP_40_40U_MHZ);
FTM_COMP(est_PHY_RESP_FTM_COMP_40_40U_MHZ_DIS);
