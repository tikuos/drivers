/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * firmware.h - C-side declarations for the embedded CYW43439 blobs.
 *
 * Backed by firmware.S (.incbin), so these symbols resolve to the
 * raw binary bytes in flash. The matching `*_size` symbols are
 * 32-bit words holding the byte length.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_CYW43_FIRMWARE_H_
#define TIKU_DRV_WIFI_CYW43_FIRMWARE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t  cyw43_firmware_data[];
extern const uint8_t  cyw43_firmware_data_end[];
extern const uint32_t cyw43_firmware_size;

extern const uint8_t  cyw43_nvram_data[];
extern const uint8_t  cyw43_nvram_data_end[];
extern const uint32_t cyw43_nvram_size;

extern const uint8_t  cyw43_clm_data[];
extern const uint8_t  cyw43_clm_data_end[];
extern const uint32_t cyw43_clm_size;

#ifdef __cplusplus
}
#endif

#endif /* TIKU_DRV_WIFI_CYW43_FIRMWARE_H_ */
