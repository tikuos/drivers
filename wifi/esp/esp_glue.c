/*
 * Tiku Drivers - ESP32-C61 radio libraries, what they expect around them
 *
 * The symbols left undefined once the libraries meet the ROM's scripts:
 * IDF's print hooks, tables and small helpers, and data the ECO4 ROM keeps
 * itself.  Mesh, ESP-NOW and FTM are not used, so theirs are inert.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include <hal/tiku_printf_hal.h>
#include "esp_abi.h"

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

/** @brief The crystal, in MHz: the DevKitC's is 40. */
uint32_t rtc_clk_xtal_freq_get(void) {
    return 40U;
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

/* ESP-NOW's vendor OUI, and mesh's auth timer: neither is used. */
uint8_t g_espnow_user_oui[3] = { 0x18, 0xFE, 0x34 };

int mesh_sta_auth_expire_time(void) {
    return 0;
}

/* Regulatory domains: filled when scanning needs them (R2). */
uint8_t regdomain_table[256];
uint8_t regulatory_data[1024];

/* FTM's per-bandwidth delay compensation, unused without FTM. */
#define FTM_COMP(name) const int32_t name = 0
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
