/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp_log.h - Native S31 BLE build declarations.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
/** @brief Write a bounded controller diagnostic to the UART. */
void s31_bt_log(const char *, ...);
#define ESP_LOGI(tag, fmt, ...) s31_bt_log(fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_EARLY_LOGW ESP_LOGW
#define ESP_EARLY_LOGE ESP_LOGE
/** @brief Accept a nonnull SDK hook; hardware reset provides modem shutdown. */
int esp_register_shutdown_handler(void (*fn)(void));
/** @brief Validate removal of the SDK shutdown hook. */
int esp_unregister_shutdown_handler(void (*fn)(void));
