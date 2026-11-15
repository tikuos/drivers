/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp_mempool.h - Controller fixed-block pools backed by caller-owned memory.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP_MEMPOOL_H_
#define TIKU_ESP_MEMPOOL_H_
#include "esp_ble_abi.h"
/** @brief Chain aligned caller-owned blocks; reject missing storage or zero block size. */
os_error_t os_mempool_init(struct os_mempool *mp, uint16_t blocks,
                         uint32_t block_size, void *membuf, const char *name);
/** @brief Initialize a fixed-block pool with optional get and put callbacks. */
os_error_t os_mempool_ext_init(struct os_mempool_ext *mp, uint16_t blocks,
                             uint32_t block_size, void *membuf, const char *name);
/** @brief Set extended-pool callbacks and their arguments. */
void os_ext_mempool_register_cb(struct os_mempool_ext *mp, void *put_cb,
                              void *put_arg, void *get_cb, void *get_arg);
/** @brief Remove a registered pool; return OS_INVALID_PARM when absent. */
os_error_t os_mempool_unregister(struct os_mempool *mp);
/** @brief Rebuild a pool's free list; callers must release outstanding blocks first. */
os_error_t os_mempool_clear(struct os_mempool *mp);
/** @brief Add flags to an initialized pool. */
void os_mempool_flags_set(struct os_mempool *mp, uint8_t flags);
/** @brief Take a free block, through an extended callback if configured; NULL if empty. */
void *os_memblock_get(struct os_mempool *mp);
/** @brief Return a block through the configured extended callback or checked free list. */
os_error_t os_memblock_put(struct os_mempool *mp, void *block);
/** @brief Return a block directly, rejecting foreign blocks and double returns. */
os_error_t os_memblock_put_from_cb(struct os_mempool *mp, void *block);
/** @brief Test membership, including interior addresses for combination pools. */
int os_memblock_from(const struct os_mempool *mp, const void *block);
/** @brief Unregister pools with the selected BTDM controller flag (bit 4).
 * The caller owns pool storage. Return OS_OK if a pool was removed, or
 * OS_INVALID_PARM if no pool matched. Outstanding blocks must be reclaimed.
 */
os_error_t espb_mempool_unregister_controller(int controller);
#endif
