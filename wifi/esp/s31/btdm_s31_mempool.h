/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * btdm_s31_mempool.h - BTDM fixed-block allocator entry points.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BTDM_S31_MEMPOOL_H_
#define TIKU_BTDM_S31_MEMPOOL_H_
#include "../esp_mempool.h"
/** @brief Initialize a BTDM pool over caller-owned aligned storage. */
os_error_t e_btdm_mempool_init(struct os_mempool *mp, uint16_t blocks,
                            uint32_t block_size, void *membuf, const char *name);
/** @brief Initialize a BTDM pool whose allocation callbacks can be replaced. */
os_error_t e_btdm_mempool_ext_init(struct os_mempool_ext *mp, uint16_t blocks,
                                uint32_t block_size, void *membuf, const char *name);
/** @brief Register extended-pool allocation callbacks. */
void e_btdm_mempool_ext_register_cb(struct os_mempool_ext *mp, void *put_cb,
                                 void *put_arg, void *get_cb, void *get_arg);
/** @brief Rebuild the free list after the caller returns outstanding blocks. */
os_error_t e_btdm_mempool_clear(struct os_mempool *mp);
/** @brief Unregister the pool without freeing its caller-owned storage. */
void e_btdm_mempool_deinit(struct os_mempool *mp);
/** @brief Add pool flags under the CPU0 interrupt guard. */
void e_btdm_mempool_flags_set(struct os_mempool *mp, uint8_t flags);
/** @brief Read the number of free blocks under the CPU0 interrupt guard. */
uint16_t e_btdm_mempool_free_blocks_num(struct os_mempool *mp);
/** @brief Test whether a block belongs to the specified pool. */
int e_btdm_memblock_from(const struct os_mempool *mp, const void *block);
/** @brief Take a block or return NULL when the pool is empty. */
void *e_btdm_memblock_get(struct os_mempool *mp);
/** @brief Return a block through the configured callback or checked free list. */
os_error_t e_btdm_memblock_put(struct os_mempool *mp, void *block);
/** @brief Return a block directly from an extended-pool callback. */
os_error_t e_btdm_memblock_put_from_cb(struct os_mempool *mp, void *block);
/** @brief Unregister pools matching the controller flag; storage remains caller-owned.
 * Return OS_OK if a pool was removed, or OS_INVALID_PARM if none matched.
 */
os_error_t e_btdm_mempool_deinit_all(bool controller);
#endif
