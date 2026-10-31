/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp_sae_config.h - Group-19 SAE crypto and allocation configuration.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ESP_SAE_CONFIG_H_
#define ESP_SAE_CONFIG_H_
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include "../esp_heap.h"
#define INCLUDES_H
#define CONFIG_SAE
#define CONFIG_ECC
#define CONFIG_NO_RANDOM_POOL
#define CONFIG_NO_STDOUT_DEBUG
#define os_malloc espw_malloc
#define os_realloc espw_realloc
#define os_free espw_free
#ifdef __riscv
#define __BYTE_ORDER __BYTE_ORDER__
#define __LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#define __BIG_ENDIAN __ORDER_BIG_ENDIAN__
#define bswap_16 __builtin_bswap16
#define bswap_32 __builtin_bswap32
#define bswap_64 __builtin_bswap64
#endif
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_WINDOW_SIZE 2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0
#define MBEDTLS_MPI_WINDOW_SIZE 1
#define MBEDTLS_MPI_MAX_SIZE 64
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_CALLOC_MACRO espw_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO espw_free
#endif
