/*
 * Tiku Drivers - ESP32-C61 BLE controller, the memory pools it imports
 *
 * The controller carves its buffers into fixed-size blocks and asks for the
 * Mynewt os_mempool calls to run them: a free list threaded through the
 * blocks themselves, and "ext" pools whose get and put the controller may
 * take over with callbacks.  Written from the API's documented behaviour,
 * as IDF's controller-only build has it (no poison or guard words).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#include <arch/esp32c61/tiku_irq_arch.h>
#include "esp_ble_abi.h"

#define POOL_ALIGN      4U

/* Every pool made, newest last: a pool made again under the same name
 * replaces the one before it, as the controller rebuilds its pools. */
static struct os_mempool *pool_list;

static uint32_t block_bytes(const struct os_mempool *mp) {
    return (mp->mp_block_size + POOL_ALIGN - 1U) & ~(POOL_ALIGN - 1U);
}

/** @brief Chain all of @p mp's blocks into its free list. */
static void pool_chain(struct os_mempool *mp) {
    uint8_t *b = (uint8_t *)(uintptr_t)mp->mp_membuf_addr;
    uint32_t step = block_bytes(mp);

    mp->mp_first = (mp->mp_num_blocks != 0U) ? (struct os_memblock *)b : NULL;
    for (uint16_t i = 1U; i <= mp->mp_num_blocks; i++, b += step) {
        ((struct os_memblock *)b)->mb_next =
            (i < mp->mp_num_blocks) ? (struct os_memblock *)(b + step) : NULL;
    }
    mp->mp_num_free = mp->mp_num_blocks;
    mp->mp_min_free = mp->mp_num_blocks;
}

os_error_t os_mempool_unregister(struct os_mempool *mp) {
    uint32_t m = tiku_esp32c61_mie_off();
    os_error_t rc = OS_INVALID_PARM;

    for (struct os_mempool **l = &pool_list; *l != NULL; l = &(*l)->mp_next) {
        if (*l == mp) {
            *l = mp->mp_next;
            mp->mp_next = NULL;
            rc = OS_OK;
            break;
        }
    }
    tiku_esp32c61_mie_restore(m);
    return rc;
}

static os_error_t pool_init(struct os_mempool *mp, uint16_t blocks,
                            uint32_t block_size, void *membuf,
                            const char *name, uint8_t flags) {
    struct os_mempool **l;
    uint32_t m;

    if (mp == NULL || block_size == 0U || (membuf == NULL && blocks != 0U)) {
        return OS_INVALID_PARM;
    }
    if (((uintptr_t)membuf & (POOL_ALIGN - 1U)) != 0U) {
        return OS_MEM_NOT_ALIGNED;
    }
    m = tiku_esp32c61_mie_off();
    for (l = &pool_list; *l != NULL;) {
        if (*l == mp || (*l)->name == name) {
            *l = (*l)->mp_next;         /* made again: the old one goes */
        } else {
            l = &(*l)->mp_next;
        }
    }
    mp->mp_block_size = block_size;
    mp->mp_num_blocks = blocks;
    mp->mp_flags = flags;
    mp->mp_membuf_addr = (uint32_t)(uintptr_t)membuf;
    mp->name = name;
    pool_chain(mp);
    mp->mp_next = NULL;
    *l = mp;                            /* l is the list's end */
    tiku_esp32c61_mie_restore(m);
    return OS_OK;
}

os_error_t os_mempool_init(struct os_mempool *mp, uint16_t blocks,
                           uint32_t block_size, void *membuf,
                           const char *name) {
    return pool_init(mp, blocks, block_size, membuf, name, 0U);
}

os_error_t os_mempool_ext_init(struct os_mempool_ext *mpe, uint16_t blocks,
                               uint32_t block_size, void *membuf,
                               const char *name) {
    os_error_t rc;

    if (mpe == NULL) {
        return OS_INVALID_PARM;
    }
    rc = pool_init(&mpe->mpe_mp, blocks, block_size, membuf, name,
                   OS_MEMPOOL_F_EXT);
    if (rc == OS_OK) {
        mpe->mpe_put_cb = NULL;
        mpe->mpe_put_arg = NULL;
        mpe->mpe_get_cb = NULL;
        mpe->mpe_get_arg = NULL;
    }
    return rc;
}

void os_ext_mempool_register_cb(struct os_mempool_ext *mpe, void *put_cb,
                                void *put_arg, void *get_cb, void *get_arg) {
    mpe->mpe_put_cb = (os_mempool_put_fn *)put_cb;
    mpe->mpe_put_arg = put_arg;
    mpe->mpe_get_cb = (os_mempool_get_fn *)get_cb;
    mpe->mpe_get_arg = get_arg;
}

os_error_t os_mempool_clear(struct os_mempool *mp) {
    uint32_t m;

    if (mp == NULL) {
        return OS_INVALID_PARM;
    }
    m = tiku_esp32c61_mie_off();
    pool_chain(mp);
    tiku_esp32c61_mie_restore(m);
    return OS_OK;
}

void os_mempool_flags_set(struct os_mempool *mp, uint8_t flags) {
    mp->mp_flags |= flags;
}

/** @brief Whether @p p is one of @p mp's blocks (any offset inside a
 *         combined pool). */
static int pool_owns(const struct os_mempool *mp, const void *p) {
    uintptr_t a = (uintptr_t)p, base = mp->mp_membuf_addr;
    uint32_t step = block_bytes(mp);

    if (a < base || a >= base + (uintptr_t)mp->mp_num_blocks * step) {
        return 0;
    }
    return (mp->mp_flags & OS_MEMPOOL_F_COMBINATION) != 0U ||
           (a - base) % step == 0U;
}

void *os_memblock_get(struct os_mempool *mp) {
    struct os_memblock *b = NULL;
    uint32_t m;

    if (mp == NULL) {
        return NULL;
    }
    if ((mp->mp_flags & OS_MEMPOOL_F_EXT) != 0U) {
        struct os_mempool_ext *mpe = (struct os_mempool_ext *)mp;

        if (mpe->mpe_get_cb != NULL) {
            return mpe->mpe_get_cb(mpe, mpe->mpe_get_arg);
        }
    }
    m = tiku_esp32c61_mie_off();
    if (mp->mp_num_free != 0U && mp->mp_first != NULL) {
        b = mp->mp_first;
        mp->mp_first = b->mb_next;
        mp->mp_num_free--;
        if (mp->mp_num_free < mp->mp_min_free) {
            mp->mp_min_free = mp->mp_num_free;
        }
    }
    tiku_esp32c61_mie_restore(m);
    return b;
}

/** @brief Back on the free list, refused if it is not the pool's, is free
 *         already, or would overfill it. */
static os_error_t pool_put(struct os_mempool *mp, void *p) {
    struct os_memblock *b = p;
    os_error_t rc = OS_INVALID_PARM;
    uint32_t m;

    if ((mp->mp_flags & OS_MEMPOOL_F_FRAG) == 0U && !pool_owns(mp, p)) {
        return OS_INVALID_PARM;
    }
    m = tiku_esp32c61_mie_off();
    if (mp->mp_num_free < mp->mp_num_blocks) {
        struct os_memblock *f = mp->mp_first;

        while (f != NULL && f != b) {
            f = f->mb_next;
        }
        if (f == NULL) {
            b->mb_next = mp->mp_first;
            mp->mp_first = b;
            mp->mp_num_free++;
            rc = OS_OK;
        }
    }
    tiku_esp32c61_mie_restore(m);
    return rc;
}

os_error_t os_memblock_put(struct os_mempool *mp, void *block) {
    if (mp == NULL || block == NULL) {
        return OS_INVALID_PARM;
    }
    if ((mp->mp_flags & OS_MEMPOOL_F_EXT) != 0U) {
        struct os_mempool_ext *mpe = (struct os_mempool_ext *)mp;

        if (mpe->mpe_put_cb != NULL) {
            return mpe->mpe_put_cb(mpe, block, mpe->mpe_put_arg);
        }
    }
    return pool_put(mp, block);
}

os_error_t os_memblock_put_from_cb(struct os_mempool *mp, void *block) {
    if (mp == NULL || block == NULL) {
        return OS_INVALID_PARM;
    }
    return pool_put(mp, block);
}

int os_memblock_from(const struct os_mempool *mp, const void *block) {
    return mp != NULL && pool_owns(mp, block);
}
