/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * btdm_s31_mempool.c - BTDM packet pools using the shared fixed-block allocator.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "btdm_s31_mempool.h"
#include "wifi_s31_arch.h"

os_error_t e_btdm_mempool_init(struct os_mempool *mp, uint16_t blocks,
                            uint32_t block_size, void *membuf, const char *name)
{ return os_mempool_init(mp, blocks, block_size, membuf, name); }
os_error_t e_btdm_mempool_ext_init(struct os_mempool_ext *mp, uint16_t blocks,
                                uint32_t block_size, void *membuf, const char *name)
{ return os_mempool_ext_init(mp, blocks, block_size, membuf, name); }
void e_btdm_mempool_ext_register_cb(struct os_mempool_ext *mp, void *put_cb,
                                 void *put_arg, void *get_cb, void *get_arg)
{ os_ext_mempool_register_cb(mp, put_cb, put_arg, get_cb, get_arg); }
os_error_t e_btdm_mempool_clear(struct os_mempool *mp)
{ return os_mempool_clear(mp); }
void e_btdm_mempool_deinit(struct os_mempool *mp)
{ (void)os_mempool_unregister(mp); }
void e_btdm_mempool_flags_set(struct os_mempool *mp, uint8_t flags)
{
    uint32_t state = espw_arch_mie_off();
    os_mempool_flags_set(mp, flags);
    espw_arch_mie_restore(state);
}
uint16_t e_btdm_mempool_free_blocks_num(struct os_mempool *mp)
{
    uint32_t state = espw_arch_mie_off();
    uint16_t count = mp->mp_num_free;
    espw_arch_mie_restore(state);
    return count;
}
int e_btdm_memblock_from(const struct os_mempool *mp, const void *block)
{ return os_memblock_from(mp, block); }
void *e_btdm_memblock_get(struct os_mempool *mp)
{ return os_memblock_get(mp); }
os_error_t e_btdm_memblock_put(struct os_mempool *mp, void *block)
{ return os_memblock_put(mp, block); }
os_error_t e_btdm_memblock_put_from_cb(struct os_mempool *mp, void *block)
{ return os_memblock_put_from_cb(mp, block); }

os_error_t e_btdm_mempool_deinit_all(bool controller)
{ return espb_mempool_unregister_controller(controller); }
