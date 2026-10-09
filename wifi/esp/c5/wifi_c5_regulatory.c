/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * C5 country/channel data from ESP-IDF 4d59230, esp_wifi_regulatory.c.
 * The table describes driver limits, not certification of the board or antenna.
 */
#include "../esp_abi.h"

typedef enum {
    ESP_WIFI_REGULATORY_TYPE_DEFAULT,
    ESP_WIFI_REGULATORY_TYPE_CE,
    ESP_WIFI_REGULATORY_TYPE_ACMA,
    ESP_WIFI_REGULATORY_TYPE_ANATEL,
    ESP_WIFI_REGULATORY_TYPE_ISED,
    ESP_WIFI_REGULATORY_TYPE_SRRC,
    ESP_WIFI_REGULATORY_TYPE_OFCA,
    ESP_WIFI_REGULATORY_TYPE_WPC,
    ESP_WIFI_REGULATORY_TYPE_MIC,
    ESP_WIFI_REGULATORY_TYPE_KCC,
    ESP_WIFI_REGULATORY_TYPE_IFETEL,
    ESP_WIFI_REGULATORY_TYPE_RCM,
    ESP_WIFI_REGULATORY_TYPE_NCC,
    ESP_WIFI_REGULATORY_TYPE_FCC,
    ESP_WIFI_REGULATORY_TYPE_AF,
    ESP_WIFI_REGULATORY_TYPE_AM,
    ESP_WIFI_REGULATORY_TYPE_AS,
    ESP_WIFI_REGULATORY_TYPE_AZ,
    ESP_WIFI_REGULATORY_TYPE_BB,
    ESP_WIFI_REGULATORY_TYPE_BD,
    ESP_WIFI_REGULATORY_TYPE_BH,
    ESP_WIFI_REGULATORY_TYPE_BN,
    ESP_WIFI_REGULATORY_TYPE_BO,
    ESP_WIFI_REGULATORY_TYPE_BS,
    ESP_WIFI_REGULATORY_TYPE_BW,
    ESP_WIFI_REGULATORY_TYPE_BZ,
    ESP_WIFI_REGULATORY_TYPE_CF,
    ESP_WIFI_REGULATORY_TYPE_CR,
    ESP_WIFI_REGULATORY_TYPE_CU,
    ESP_WIFI_REGULATORY_TYPE_DM,
    ESP_WIFI_REGULATORY_TYPE_DZ,
    ESP_WIFI_REGULATORY_TYPE_EC,
    ESP_WIFI_REGULATORY_TYPE_EG,
    ESP_WIFI_REGULATORY_TYPE_FO,
    ESP_WIFI_REGULATORY_TYPE_GB,
    ESP_WIFI_REGULATORY_TYPE_GD,
    ESP_WIFI_REGULATORY_TYPE_GT,
    ESP_WIFI_REGULATORY_TYPE_GU,
    ESP_WIFI_REGULATORY_TYPE_HR,
    ESP_WIFI_REGULATORY_TYPE_ID,
    ESP_WIFI_REGULATORY_TYPE_IR,
    ESP_WIFI_REGULATORY_TYPE_JO,
    ESP_WIFI_REGULATORY_TYPE_KE,
    ESP_WIFI_REGULATORY_TYPE_KN,
    ESP_WIFI_REGULATORY_TYPE_KP,
    ESP_WIFI_REGULATORY_TYPE_KW,
    ESP_WIFI_REGULATORY_TYPE_KY,
    ESP_WIFI_REGULATORY_TYPE_KZ,
    ESP_WIFI_REGULATORY_TYPE_LK,
    ESP_WIFI_REGULATORY_TYPE_MA,
    ESP_WIFI_REGULATORY_TYPE_MO,
    ESP_WIFI_REGULATORY_TYPE_MV,
    ESP_WIFI_REGULATORY_TYPE_MY,
    ESP_WIFI_REGULATORY_TYPE_NA,
    ESP_WIFI_REGULATORY_TYPE_OM,
    ESP_WIFI_REGULATORY_TYPE_PA,
    ESP_WIFI_REGULATORY_TYPE_PH,
    ESP_WIFI_REGULATORY_TYPE_PK,
    ESP_WIFI_REGULATORY_TYPE_QA,
    ESP_WIFI_REGULATORY_TYPE_RS,
    ESP_WIFI_REGULATORY_TYPE_RU,
    ESP_WIFI_REGULATORY_TYPE_SG,
    ESP_WIFI_REGULATORY_TYPE_SV,
    ESP_WIFI_REGULATORY_TYPE_SX,
    ESP_WIFI_REGULATORY_TYPE_SY,
    ESP_WIFI_REGULATORY_TYPE_TG,
    ESP_WIFI_REGULATORY_TYPE_TR,
    ESP_WIFI_REGULATORY_TYPE_UA,
    ESP_WIFI_REGULATORY_TYPE_VE,
    ESP_WIFI_REGULATORY_TYPE_VN,
    ESP_WIFI_REGULATORY_TYPE_WS,
    ESP_WIFI_REGULATORY_TYPE_YE,
    ESP_WIFI_REGULATORY_TYPE_ZA,
    ESP_WIFI_REGULATORY_TYPE_MAX,
} esp_wifi_regulatory_type_t;

/*
 * regdomain_table: ISO alpha-2 country codes and their regulatory profile indices.
 * Last row is a sentinel (not a real country code).
 */
const wifi_regdomain_t regdomain_table[] = {
    {{'0', '1'}, ESP_WIFI_REGULATORY_TYPE_DEFAULT},
    {{'E', 'U'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'A', 'D'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'A', 'E'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'A', 'F'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'A', 'I'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'A', 'L'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'A', 'M'}, ESP_WIFI_REGULATORY_TYPE_AM},
    {{'A', 'N'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'A', 'R'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'A', 'S'}, ESP_WIFI_REGULATORY_TYPE_AS},
    {{'A', 'T'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'A', 'U'}, ESP_WIFI_REGULATORY_TYPE_ACMA},
    {{'A', 'W'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'A', 'Z'}, ESP_WIFI_REGULATORY_TYPE_AZ},
    {{'B', 'A'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'B', 'B'}, ESP_WIFI_REGULATORY_TYPE_BB},
    {{'B', 'D'}, ESP_WIFI_REGULATORY_TYPE_BD},
    {{'B', 'E'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'B', 'F'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'B', 'G'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'B', 'H'}, ESP_WIFI_REGULATORY_TYPE_BH},
    {{'B', 'L'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'B', 'M'}, ESP_WIFI_REGULATORY_TYPE_AS},
    {{'B', 'N'}, ESP_WIFI_REGULATORY_TYPE_BN},
    {{'B', 'O'}, ESP_WIFI_REGULATORY_TYPE_BO},
    {{'B', 'R'}, ESP_WIFI_REGULATORY_TYPE_ANATEL},
    {{'B', 'S'}, ESP_WIFI_REGULATORY_TYPE_BS},
    {{'B', 'T'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'B', 'W'}, ESP_WIFI_REGULATORY_TYPE_BW},
    {{'B', 'Y'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'B', 'Z'}, ESP_WIFI_REGULATORY_TYPE_BZ},
    {{'C', 'A'}, ESP_WIFI_REGULATORY_TYPE_ISED},
    {{'C', 'F'}, ESP_WIFI_REGULATORY_TYPE_CF},
    {{'C', 'H'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'C', 'I'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'C', 'L'}, ESP_WIFI_REGULATORY_TYPE_BN},
    {{'C', 'N'}, ESP_WIFI_REGULATORY_TYPE_SRRC},
    {{'C', 'O'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'C', 'R'}, ESP_WIFI_REGULATORY_TYPE_CR},
    {{'C', 'U'}, ESP_WIFI_REGULATORY_TYPE_CU},
    {{'C', 'X'}, ESP_WIFI_REGULATORY_TYPE_BS},
    {{'C', 'Y'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'C', 'Z'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'D', 'E'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'D', 'K'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'D', 'M'}, ESP_WIFI_REGULATORY_TYPE_DM},
    {{'D', 'O'}, ESP_WIFI_REGULATORY_TYPE_DM},
    {{'D', 'Z'}, ESP_WIFI_REGULATORY_TYPE_DZ},
    {{'E', 'C'}, ESP_WIFI_REGULATORY_TYPE_EC},
    {{'E', 'E'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'E', 'G'}, ESP_WIFI_REGULATORY_TYPE_EG},
    {{'E', 'S'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'E', 'T'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'F', 'I'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'F', 'M'}, ESP_WIFI_REGULATORY_TYPE_AS},
    {{'F', 'O'}, ESP_WIFI_REGULATORY_TYPE_FO},
    {{'F', 'R'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'G', 'B'}, ESP_WIFI_REGULATORY_TYPE_GB},
    {{'G', 'D'}, ESP_WIFI_REGULATORY_TYPE_GD},
    {{'G', 'E'}, ESP_WIFI_REGULATORY_TYPE_AZ},
    {{'G', 'F'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'G', 'H'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'G', 'I'}, ESP_WIFI_REGULATORY_TYPE_FO},
    {{'G', 'L'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'G', 'P'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'G', 'R'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'G', 'T'}, ESP_WIFI_REGULATORY_TYPE_GT},
    {{'G', 'U'}, ESP_WIFI_REGULATORY_TYPE_GU},
    {{'G', 'Y'}, ESP_WIFI_REGULATORY_TYPE_NCC},
    {{'H', 'K'}, ESP_WIFI_REGULATORY_TYPE_OFCA},
    {{'H', 'N'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'H', 'R'}, ESP_WIFI_REGULATORY_TYPE_HR},
    {{'H', 'T'}, ESP_WIFI_REGULATORY_TYPE_AS},
    {{'H', 'U'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'I', 'D'}, ESP_WIFI_REGULATORY_TYPE_ID},
    {{'I', 'E'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'I', 'L'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'I', 'M'}, ESP_WIFI_REGULATORY_TYPE_FO},
    {{'I', 'N'}, ESP_WIFI_REGULATORY_TYPE_WPC},
    {{'I', 'R'}, ESP_WIFI_REGULATORY_TYPE_IR},
    {{'I', 'S'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'I', 'T'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'J', 'M'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'J', 'O'}, ESP_WIFI_REGULATORY_TYPE_JO},
    {{'J', 'P'}, ESP_WIFI_REGULATORY_TYPE_MIC},
    {{'K', 'E'}, ESP_WIFI_REGULATORY_TYPE_KE},
    {{'K', 'H'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'K', 'N'}, ESP_WIFI_REGULATORY_TYPE_KN},
    {{'K', 'P'}, ESP_WIFI_REGULATORY_TYPE_KP},
    {{'K', 'R'}, ESP_WIFI_REGULATORY_TYPE_KCC},
    {{'K', 'W'}, ESP_WIFI_REGULATORY_TYPE_KW},
    {{'K', 'Y'}, ESP_WIFI_REGULATORY_TYPE_KY},
    {{'K', 'Z'}, ESP_WIFI_REGULATORY_TYPE_KZ},
    {{'L', 'B'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'L', 'C'}, ESP_WIFI_REGULATORY_TYPE_KN},
    {{'L', 'I'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'L', 'K'}, ESP_WIFI_REGULATORY_TYPE_LK},
    {{'L', 'S'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'L', 'T'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'L', 'U'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'L', 'V'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'M', 'A'}, ESP_WIFI_REGULATORY_TYPE_MA},
    {{'M', 'C'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'M', 'D'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'M', 'E'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'M', 'F'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'M', 'H'}, ESP_WIFI_REGULATORY_TYPE_AS},
    {{'M', 'K'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'M', 'N'}, ESP_WIFI_REGULATORY_TYPE_BS},
    {{'M', 'O'}, ESP_WIFI_REGULATORY_TYPE_MO},
    {{'M', 'P'}, ESP_WIFI_REGULATORY_TYPE_AS},
    {{'M', 'Q'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'M', 'R'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'M', 'T'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'M', 'U'}, ESP_WIFI_REGULATORY_TYPE_BS},
    {{'M', 'V'}, ESP_WIFI_REGULATORY_TYPE_MV},
    {{'M', 'W'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'M', 'X'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'M', 'Y'}, ESP_WIFI_REGULATORY_TYPE_MY},
    {{'N', 'A'}, ESP_WIFI_REGULATORY_TYPE_NA},
    {{'N', 'G'}, ESP_WIFI_REGULATORY_TYPE_BO},
    {{'N', 'I'}, ESP_WIFI_REGULATORY_TYPE_AS},
    {{'N', 'L'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'N', 'O'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'N', 'P'}, ESP_WIFI_REGULATORY_TYPE_BN},
    {{'N', 'Z'}, ESP_WIFI_REGULATORY_TYPE_RCM},
    {{'O', 'M'}, ESP_WIFI_REGULATORY_TYPE_OM},
    {{'P', 'A'}, ESP_WIFI_REGULATORY_TYPE_PA},
    {{'P', 'E'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'P', 'F'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'P', 'G'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'P', 'H'}, ESP_WIFI_REGULATORY_TYPE_PH},
    {{'P', 'K'}, ESP_WIFI_REGULATORY_TYPE_PK},
    {{'P', 'L'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'P', 'M'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'P', 'R'}, ESP_WIFI_REGULATORY_TYPE_GD},
    {{'P', 'T'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'P', 'W'}, ESP_WIFI_REGULATORY_TYPE_AS},
    {{'P', 'Y'}, ESP_WIFI_REGULATORY_TYPE_BS},
    {{'Q', 'A'}, ESP_WIFI_REGULATORY_TYPE_QA},
    {{'R', 'E'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'R', 'O'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'R', 'S'}, ESP_WIFI_REGULATORY_TYPE_RS},
    {{'R', 'U'}, ESP_WIFI_REGULATORY_TYPE_RU},
    {{'R', 'W'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'S', 'A'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'S', 'E'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'S', 'G'}, ESP_WIFI_REGULATORY_TYPE_SG},
    {{'S', 'I'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'S', 'K'}, ESP_WIFI_REGULATORY_TYPE_CE},
    {{'S', 'M'}, ESP_WIFI_REGULATORY_TYPE_FO},
    {{'S', 'N'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'S', 'R'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'S', 'V'}, ESP_WIFI_REGULATORY_TYPE_SV},
    {{'S', 'X'}, ESP_WIFI_REGULATORY_TYPE_SX},
    {{'S', 'Y'}, ESP_WIFI_REGULATORY_TYPE_SY},
    {{'T', 'C'}, ESP_WIFI_REGULATORY_TYPE_BS},
    {{'T', 'D'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'T', 'G'}, ESP_WIFI_REGULATORY_TYPE_TG},
    {{'T', 'H'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'T', 'N'}, ESP_WIFI_REGULATORY_TYPE_MA},
    {{'T', 'R'}, ESP_WIFI_REGULATORY_TYPE_TR},
    {{'T', 'T'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'T', 'W'}, ESP_WIFI_REGULATORY_TYPE_NCC},
    {{'T', 'Z'}, ESP_WIFI_REGULATORY_TYPE_BW},
    {{'U', 'A'}, ESP_WIFI_REGULATORY_TYPE_UA},
    {{'U', 'G'}, ESP_WIFI_REGULATORY_TYPE_BS},
    {{'U', 'S'}, ESP_WIFI_REGULATORY_TYPE_FCC},
    {{'U', 'Y'}, ESP_WIFI_REGULATORY_TYPE_BB},
    {{'U', 'Z'}, ESP_WIFI_REGULATORY_TYPE_MA},
    {{'V', 'A'}, ESP_WIFI_REGULATORY_TYPE_FO},
    {{'V', 'C'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'V', 'E'}, ESP_WIFI_REGULATORY_TYPE_VE},
    {{'V', 'I'}, ESP_WIFI_REGULATORY_TYPE_AS},
    {{'V', 'N'}, ESP_WIFI_REGULATORY_TYPE_VN},
    {{'V', 'U'}, ESP_WIFI_REGULATORY_TYPE_IFETEL},
    {{'W', 'F'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'W', 'S'}, ESP_WIFI_REGULATORY_TYPE_WS},
    {{'Y', 'E'}, ESP_WIFI_REGULATORY_TYPE_YE},
    {{'Y', 'T'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'Z', 'A'}, ESP_WIFI_REGULATORY_TYPE_ZA},
    {{'Z', 'W'}, ESP_WIFI_REGULATORY_TYPE_AF},
    {{'#', '#'}, ESP_WIFI_REGULATORY_TYPE_MAX}, /* Sentinel: end of table; not a real country code — do not use as wifi_country_t.cc. */
};

const wifi_regulatory_t regulatory_data[] = {
    /* ESP_WIFI_REGULATORY_TYPE_DEFAULT */
    {
        3,  /* Number of rules */
        {
            { 1, 11, 2, 20, 0, 0 },  /* chan:1~11, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 20, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_CE */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 26, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:26(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 13, 0, 0 },  /* chan:149~173, max_bw:80M, max_power:13(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_ACMA */
    {
        7,  /* Number of rules */
        {
            { 1, 13, 2, 36, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:36(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 116, 3, 26, 1, 0 },  /* chan:100~116, max_bw:80M, max_power:26(dBm), dfs:1, reserved.*/
            { 132, 144, 3, 26, 1, 0 },  /* chan:132~144, max_bw:80M, max_power:26(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 36, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:36(dBm), dfs:0, reserved.*/
            { 173, 173, 1, 13, 0, 0 },  /* chan:173~173, max_bw:20M, max_power:13(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_ANATEL */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 30, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 27, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:27(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 27, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:27(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 27, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:27(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_ISED */
    {
        6,  /* Number of rules */
        {
            { 1, 13, 2, 36, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:36(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 26, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:26(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 26, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:26(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 36, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:36(dBm), dfs:0, reserved.*/
            { 173, 177, 2, 27, 0, 0 },  /* chan:173~177, max_bw:40M, max_power:27(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_SRRC */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 33, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:33(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_OFCA */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 36, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:36(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 27, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:27(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 36, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:36(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_WPC */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 30, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 30, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 24, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 24, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 30, 0, 0 },  /* chan:149~173, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_MIC */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 14, 14, 1, 20, 0, 0 },  /* chan:14~14, max_bw:20M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 20, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 23, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:23(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_KCC */
    {
        6,  /* Number of rules */
        {
            { 1, 13, 2, 23, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:23(dBm), dfs:0, reserved.*/
            { 36, 44, 2, 23, 0, 0 },  /* chan:36~44, max_bw:40M, max_power:23(dBm), dfs:0, reserved.*/
            { 48, 48, 1, 17, 0, 0 },  /* chan:48~48, max_bw:20M, max_power:17(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 20, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:20(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 23, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_IFETEL */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 17, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:17(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 24, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 24, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_RCM */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 36, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:36(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 30, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 27, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:27(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 27, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:27(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 36, 0, 0 },  /* chan:149~173, max_bw:80M, max_power:36(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_NCC */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 30, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 23, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:23(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_FCC */
    {
        6,  /* Number of rules */
        {
            { 1, 11, 2, 30, 0, 0 },  /* chan:1~11, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 24, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 24, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
            { 173, 177, 2, 27, 0, 0 },  /* chan:173~177, max_bw:40M, max_power:27(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_AF */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 20, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 27, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:27(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_AM */
    {
        3,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 64, 4, 17, 1, 0 },  /* chan:36~64, max_bw:160M, max_power:17(dBm), dfs:1, reserved.*/
            { 100, 173, 4, 17, 1, 0 },  /* chan:100~173, max_bw:160M, max_power:17(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_AS */
    {
        5,  /* Number of rules */
        {
            { 1, 11, 2, 30, 0, 0 },  /* chan:1~11, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 24, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:24(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 24, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 24, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_AZ */
    {
        3,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 18, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:18(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 18, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:18(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_BB */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_BD */
    {
        2,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_BH */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 64, 3, 23, 1, 0 },  /* chan:36~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 140, 3, 27, 1, 0 },  /* chan:100~140, max_bw:80M, max_power:27(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 24, 1, 0 },  /* chan:149~173, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_BN */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 20, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 20, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_BO */
    {
        3,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 30, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:30(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_BS */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 24, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:24(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 24, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 24, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_BW */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 23, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:23(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 24, 1, 0 },  /* chan:149~165, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_BZ */
    {
        2,  /* Number of rules */
        {
            { 1, 13, 2, 30, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_CF */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 2, 17, 0, 0 },  /* chan:36~48, max_bw:40M, max_power:17(dBm), dfs:0, reserved.*/
            { 52, 64, 2, 24, 1, 0 },  /* chan:52~64, max_bw:40M, max_power:24(dBm), dfs:1, reserved.*/
            { 100, 144, 2, 24, 1, 0 },  /* chan:100~144, max_bw:40M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 165, 2, 30, 0, 0 },  /* chan:149~165, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_CR */
    {
        6,  /* Number of rules */
        {
            { 1, 13, 2, 36, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:36(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 30, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 30, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:30(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 30, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:30(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 36, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:36(dBm), dfs:0, reserved.*/
            { 173, 177, 2, 36, 0, 0 },  /* chan:173~177, max_bw:40M, max_power:36(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_CU */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 23, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:23(dBm), dfs:0, reserved.*/
            { 36, 64, 3, 23, 0, 0 },  /* chan:36~64, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 100, 140, 3, 23, 0, 0 },  /* chan:100~140, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 149, 165, 3, 23, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_DM */
    {
        4,  /* Number of rules */
        {
            { 1, 11, 2, 30, 0, 0 },  /* chan:1~11, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 17, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:17(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_DZ */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 132, 4, 23, 1, 0 },  /* chan:100~132, max_bw:160M, max_power:23(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_EC */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 30, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 16, 1, 0 },  /* chan:36~48, max_bw:80M, max_power:16(dBm), dfs:1, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 20, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:20(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_EG */
    {
        3,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_FO */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 26, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:26(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_GB */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 26, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:26(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 23, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_GD */
    {
        5,  /* Number of rules */
        {
            { 1, 11, 2, 30, 0, 0 },  /* chan:1~11, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 17, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:17(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 24, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 24, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_GT */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 26, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:26(dBm), dfs:0, reserved.*/
            { 36, 64, 3, 23, 0, 0 },  /* chan:36~64, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 100, 140, 4, 23, 0, 0 },  /* chan:100~140, max_bw:160M, max_power:23(dBm), dfs:0, reserved.*/
            { 149, 165, 3, 26, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:26(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_GU */
    {
        5,  /* Number of rules */
        {
            { 1, 11, 2, 30, 0, 0 },  /* chan:1~11, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 1, 17, 0, 0 },  /* chan:36~48, max_bw:20M, max_power:17(dBm), dfs:0, reserved.*/
            { 52, 64, 1, 24, 1, 0 },  /* chan:52~64, max_bw:20M, max_power:24(dBm), dfs:1, reserved.*/
            { 100, 144, 1, 24, 1, 0 },  /* chan:100~144, max_bw:20M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 165, 1, 30, 0, 0 },  /* chan:149~165, max_bw:20M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_HR */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 26, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:26(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 13, 0, 0 },  /* chan:149~173, max_bw:80M, max_power:13(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_ID */
    {
        3,  /* Number of rules */
        {
            { 1, 13, 2, 26, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:26(dBm), dfs:0, reserved.*/
            { 36, 64, 4, 23, 0, 0 },  /* chan:36~64, max_bw:160M, max_power:23(dBm), dfs:0, reserved.*/
            { 149, 161, 3, 23, 0, 0 },  /* chan:149~161, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_IR */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 27, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:27(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_JO */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 140, 3, 27, 1, 0 },  /* chan:100~140, max_bw:80M, max_power:27(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 23, 0, 0 },  /* chan:149~173, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_KE */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 33, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:33(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 17, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:17(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 17, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:17(dBm), dfs:1, reserved.*/
            { 100, 140, 3, 24, 1, 0 },  /* chan:100~140, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 173, 2, 24, 1, 0 },  /* chan:149~173, max_bw:40M, max_power:24(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_KN */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 20, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 30, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:30(dBm), dfs:1, reserved.*/
            { 149, 161, 3, 30, 0, 0 },  /* chan:149~161, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_KP */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 1, 20, 0, 0 },  /* chan:1~13, max_bw:20M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 1, 20, 0, 0 },  /* chan:36~48, max_bw:20M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 1, 20, 1, 0 },  /* chan:52~64, max_bw:20M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 124, 1, 30, 1, 0 },  /* chan:100~124, max_bw:20M, max_power:30(dBm), dfs:1, reserved.*/
            { 149, 161, 1, 30, 0, 0 },  /* chan:149~161, max_bw:20M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_KW */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 17, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:17(dBm), dfs:1, reserved.*/
            { 100, 161, 4, 24, 1, 0 },  /* chan:100~161, max_bw:160M, max_power:24(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_KY */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 30, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 23, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:23(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 30, 0, 0 },  /* chan:149~173, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_KZ */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 20, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:20(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 20, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_LK */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 1, 17, 0, 0 },  /* chan:36~48, max_bw:20M, max_power:17(dBm), dfs:0, reserved.*/
            { 52, 64, 1, 24, 1, 0 },  /* chan:52~64, max_bw:20M, max_power:24(dBm), dfs:1, reserved.*/
            { 100, 144, 1, 24, 1, 0 },  /* chan:100~144, max_bw:20M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 165, 1, 30, 0, 0 },  /* chan:149~165, max_bw:20M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_MA */
    {
        3,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 20, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_MO */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 23, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:23(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 30, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:30(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_MV */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 20, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_MY */
    {
        5,  /* Number of rules */
        {
            { 1, 14, 2, 26, 0, 0 },  /* chan:1~14, max_bw:40M, max_power:26(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 30, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 128, 4, 23, 1, 0 },  /* chan:100~128, max_bw:160M, max_power:23(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 30, 0, 0 },  /* chan:149~173, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_NA */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 20, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 21, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:21(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 24, 1, 0 },  /* chan:149~173, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_OM */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 27, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:27(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 28, 1, 0 },  /* chan:149~165, max_bw:80M, max_power:28(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_PA */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 36, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:36(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 36, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:36(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 30, 0, 0 },  /* chan:52~64, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
            { 100, 140, 4, 30, 0, 0 },  /* chan:100~140, max_bw:160M, max_power:30(dBm), dfs:0, reserved.*/
            { 149, 165, 3, 36, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:36(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_PH */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 24, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:24(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 24, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:24(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_PK */
    {
        2,  /* Number of rules */
        {
            { 1, 14, 2, 30, 0, 0 },  /* chan:1~14, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 149, 173, 3, 30, 0, 0 },  /* chan:149~173, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_QA */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 20, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:20(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 20, 1, 0 },  /* chan:149~173, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_RS */
    {
        6,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 27, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:27(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 24, 1, 0 },  /* chan:149~165, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
            { 173, 173, 1, 24, 0, 0 },  /* chan:173~173, max_bw:20M, max_power:24(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_RU */
    {
        3,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 64, 4, 20, 0, 0 },  /* chan:36~64, max_bw:160M, max_power:20(dBm), dfs:0, reserved.*/
            { 132, 165, 4, 23, 0, 0 },  /* chan:132~165, max_bw:160M, max_power:23(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_SG */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 23, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:23(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 144, 4, 26, 1, 0 },  /* chan:100~144, max_bw:160M, max_power:26(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_SV */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 1, 17, 0, 0 },  /* chan:36~48, max_bw:20M, max_power:17(dBm), dfs:0, reserved.*/
            { 52, 64, 1, 23, 1, 0 },  /* chan:52~64, max_bw:20M, max_power:23(dBm), dfs:1, reserved.*/
            { 149, 165, 1, 30, 0, 0 },  /* chan:149~165, max_bw:20M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_SX */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 16, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:16(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 21, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:21(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 23, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:23(dBm), dfs:1, reserved.*/
            { 149, 173, 3, 24, 1, 0 },  /* chan:149~173, max_bw:80M, max_power:24(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_SY */
    {
        6,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 23, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:23(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 23, 1, 0 },  /* chan:149~165, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 173, 173, 1, 23, 0, 0 },  /* chan:173~173, max_bw:20M, max_power:23(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_TG */
    {
        3,  /* Number of rules */
        {
            { 1, 13, 2, 20, 1, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:1, reserved.*/
            { 36, 64, 3, 20, 1, 0 },  /* chan:36~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 165, 4, 27, 1, 0 },  /* chan:100~165, max_bw:160M, max_power:27(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_TR */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 27, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:27(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_UA */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 20, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 20, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:20(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 20, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_VE */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 30, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:30(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 23, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:23(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_VN */
    {
        5,  /* Number of rules */
        {
            { 1, 13, 2, 23, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:23(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 23, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:23(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 26, 1, 0 },  /* chan:100~140, max_bw:160M, max_power:26(dBm), dfs:1, reserved.*/
            { 149, 165, 3, 30, 0, 0 },  /* chan:149~165, max_bw:80M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_WS */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 2, 20, 0, 0 },  /* chan:36~48, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 2, 20, 1, 0 },  /* chan:52~64, max_bw:40M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 2, 27, 1, 0 },  /* chan:100~140, max_bw:40M, max_power:27(dBm), dfs:1, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_YE */
    {
        1,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
        }
    },
    /* ESP_WIFI_REGULATORY_TYPE_ZA */
    {
        4,  /* Number of rules */
        {
            { 1, 13, 2, 20, 0, 0 },  /* chan:1~13, max_bw:40M, max_power:20(dBm), dfs:0, reserved.*/
            { 36, 48, 3, 20, 0, 0 },  /* chan:36~48, max_bw:80M, max_power:20(dBm), dfs:0, reserved.*/
            { 52, 64, 3, 20, 1, 0 },  /* chan:52~64, max_bw:80M, max_power:20(dBm), dfs:1, reserved.*/
            { 100, 140, 4, 30, 0, 0 },  /* chan:100~140, max_bw:160M, max_power:30(dBm), dfs:0, reserved.*/
        }
    },
};
