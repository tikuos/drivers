/*
 * Tiku Drivers - ESP32-C61 radio libraries, their heap
 *
 * Two regions, each a free list in address order so a free merges with both
 * neighbours: internal SRAM (up to two blocks, one per radio up) for the
 * libraries' own state, and an optional PSRAM block that their packet
 * buffers prefer, as IDF's SPIRAM_TRY_ALLOCATE_WIFI_LWIP has them.  A block
 * in use carries its size and a magic word; freeing one without it is
 * reported, not obeyed.  Interrupts are masked inside, since the libraries'
 * interrupt handlers allocate too.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <hal/tiku_printf_hal.h>
#include <arch/esp32c61/tiku_irq_arch.h>
#include "esp_heap.h"

#define HDR         8U                  /* size word + magic, keeps 8-align */
#define MIN_BLOCK   16U
#define USED_MAGIC  0xE5A11C0DUL
#define SPANS       2U

/** @brief A free block: its size (header included) and the next one. */
typedef struct espw_blk {
    uint32_t         size;
    struct espw_blk *next;
} espw_blk_t;

/** @brief One region: its blocks' memory and the free list over them. */
typedef struct {
    espw_blk_t *free_list;
    uint8_t    *base[SPANS];
    uint32_t    len[SPANS];
    uint32_t    size;               /* all spans */
    uint32_t    free_bytes;
    uint32_t    low;
    uint32_t    fails;
} espw_region_t;

static espw_region_t reg_int;           /* SRAM */
static espw_region_t reg_ext;           /* PSRAM */

/* The lock pool: slots of 16 bytes, one bit each. */
#define OBJ_SLOTS   48U
#define OBJ_BYTES   16U

static uint32_t obj_pool[OBJ_SLOTS][OBJ_BYTES / 4U];
static uint64_t obj_used;

/*---------------------------------------------------------------------------*/
/* One region                                                                */
/*---------------------------------------------------------------------------*/

/** @brief Put a free block into @p r's list in address order, merged with
 *         whichever neighbour touches it; interrupts masked. */
static void region_insert(espw_region_t *r, espw_blk_t *b) {
    espw_blk_t *prev = NULL, *cur;

    for (cur = r->free_list; cur != NULL && cur < b; cur = cur->next) {
        prev = cur;
    }
    b->next = cur;
    if (cur != NULL && (uint8_t *)b + b->size == (uint8_t *)cur) {
        b->size += cur->size;
        b->next = cur->next;
    }
    if (prev != NULL && (uint8_t *)prev + prev->size == (uint8_t *)b) {
        prev->size += b->size;
        prev->next = b->next;
    } else if (prev != NULL) {
        prev->next = b;
    } else {
        r->free_list = b;
    }
}

/** @brief Give @p r the @p len bytes at @p base, 8-byte aligned. */
static int region_add(espw_region_t *r, void *base, uint32_t len) {
    uintptr_t b = ((uintptr_t)base + 7U) & ~(uintptr_t)7U;
    uint32_t m;
    unsigned i;

    len -= (uint32_t)(b - (uintptr_t)base);
    len &= ~7UL;
    for (i = 0U; i < SPANS && r->base[i] != NULL; i++) {
    }
    if (i == SPANS || len < MIN_BLOCK) {
        return -1;
    }
    m = tiku_esp32c61_mie_off();
    r->base[i] = (uint8_t *)b;
    r->len[i] = len;
    r->size += len;
    r->free_bytes += len;
    r->low += len;
    ((espw_blk_t *)b)->size = len;
    region_insert(r, (espw_blk_t *)b);
    tiku_esp32c61_mie_restore(m);
    return 0;
}

static void region_reset(espw_region_t *r) {
    uint32_t m = tiku_esp32c61_mie_off();

    memset(r, 0, sizeof *r);
    tiku_esp32c61_mie_restore(m);
}

/** @brief Whether @p p lies in one of @p r's blocks. */
static int region_has(const espw_region_t *r, const void *p) {
    const uint8_t *a = p;

    for (unsigned i = 0U; i < SPANS; i++) {
        if (r->base[i] != NULL && a >= r->base[i] &&
            a < r->base[i] + r->len[i]) {
            return 1;
        }
    }
    return 0;
}

static void *region_alloc(espw_region_t *r, size_t n, int count_fail) {
    espw_blk_t **link, *b;
    uint32_t *hdr = NULL;
    uint32_t need, m;

    if (n == 0U || n > r->size) {
        if (count_fail && n != 0U) {
            r->fails++;
        }
        return NULL;
    }
    need = (((uint32_t)n + 7U) & ~7UL) + HDR;
    m = tiku_esp32c61_mie_off();
    for (link = &r->free_list; (b = *link) != NULL; link = &b->next) {
        if (b->size < need) {
            continue;
        }
        if (b->size - need >= MIN_BLOCK) {
            espw_blk_t *rest = (espw_blk_t *)((uint8_t *)b + need);

            rest->size = b->size - need;
            rest->next = b->next;
            *link = rest;
            b->size = need;
        } else {
            *link = b->next;
        }
        r->free_bytes -= b->size;
        if (r->free_bytes < r->low) {
            r->low = r->free_bytes;
        }
        hdr = (uint32_t *)b;
        hdr[1] = USED_MAGIC;
        break;
    }
    if (hdr == NULL && count_fail) {
        r->fails++;
    }
    tiku_esp32c61_mie_restore(m);
    return hdr != NULL ? (uint8_t *)hdr + HDR : NULL;
}

/** @brief The region and block header @p p was handed out from. */
static espw_region_t *region_of(void *p, uint32_t **hdr_out) {
    uint32_t *hdr = (uint32_t *)((uint8_t *)p - HDR);
    espw_region_t *r = region_has(&reg_ext, hdr) ? &reg_ext
                     : region_has(&reg_int, hdr) ? &reg_int : NULL;

    if (r == NULL || ((uintptr_t)hdr & 7U) != 0U || hdr[1] != USED_MAGIC) {
        TIKU_PRINTF("[esp-wifi] free of 0x%08lx: not a heap block\n",
                    (unsigned long)(uintptr_t)p);
        return NULL;
    }
    *hdr_out = hdr;
    return r;
}

static void region_stats(const espw_region_t *r, espw_heap_stats_t *out) {
    uint32_t m = tiku_esp32c61_mie_off();
    uint32_t big = 0U;

    for (espw_blk_t *b = r->free_list; b != NULL; b = b->next) {
        if (b->size > big) {
            big = b->size;
        }
    }
    out->size = r->size;
    out->free = r->free_bytes;
    out->low = r->low;
    out->largest = big > HDR ? big - HDR : 0U;
    out->fails = r->fails;
    tiku_esp32c61_mie_restore(m);
}

/*---------------------------------------------------------------------------*/
/* The heap                                                                  */
/*---------------------------------------------------------------------------*/

void espw_heap_init(void *base, uint32_t len) {
    region_reset(&reg_int);
    (void)region_add(&reg_int, base, len);
}

int espw_heap_grow(void *base, uint32_t len) {
    return region_add(&reg_int, base, len);
}

void espw_heap_reset(void) {
    region_reset(&reg_int);
}

uint32_t espw_heap_used(void) {
    return reg_int.size - reg_int.free_bytes;
}

void espw_heap_ext_init(void *base, uint32_t len) {
    region_reset(&reg_ext);
    (void)region_add(&reg_ext, base, len);
}

void espw_heap_ext_reset(void) {
    region_reset(&reg_ext);
}

uint32_t espw_heap_ext_used(void) {
    return reg_ext.size - reg_ext.free_bytes;
}

void *espw_malloc(size_t n) {
    return region_alloc(&reg_int, n, 1);
}

void *espw_malloc_ext(size_t n) {
    void *p = region_alloc(&reg_ext, n, 0);

    return p != NULL ? p : region_alloc(&reg_int, n, 1);
}

static void *calloc_in(void *(*alloc)(size_t), size_t count, size_t n) {
    size_t total = count * n;
    void *p;

    if (n != 0U && total / n != count) {
        return NULL;
    }
    p = alloc(total);
    if (p != NULL) {
        memset(p, 0, total);
    }
    return p;
}

void *espw_calloc(size_t count, size_t n) {
    return calloc_in(espw_malloc, count, n);
}

void *espw_calloc_ext(size_t count, size_t n) {
    return calloc_in(espw_malloc_ext, count, n);
}

void espw_free(void *p) {
    espw_region_t *r;
    uint32_t *hdr;
    uint32_t m;

    if (p == NULL || (r = region_of(p, &hdr)) == NULL) {
        return;
    }
    m = tiku_esp32c61_mie_off();
    hdr[1] = 0U;
    r->free_bytes += hdr[0];
    region_insert(r, (espw_blk_t *)hdr);
    tiku_esp32c61_mie_restore(m);
}

static void *realloc_in(void *(*alloc)(size_t), void *p, size_t n) {
    uint32_t *hdr;
    uint32_t have;
    void *q;

    if (p == NULL) {
        return alloc(n);
    }
    if (n == 0U) {
        espw_free(p);
        return NULL;
    }
    if (region_of(p, &hdr) == NULL) {
        return NULL;
    }
    have = hdr[0] - HDR;
    if (n <= have) {
        return p;
    }
    q = alloc(n);
    if (q != NULL) {
        memcpy(q, p, have);
        espw_free(p);
    }
    return q;
}

void *espw_realloc(void *p, size_t n) {
    return realloc_in(espw_malloc, p, n);
}

void *espw_realloc_ext(void *p, size_t n) {
    return realloc_in(espw_malloc_ext, p, n);
}

void espw_heap_stats(espw_heap_stats_t *out) {
    region_stats(&reg_int, out);
}

void espw_heap_ext_stats(espw_heap_stats_t *out) {
    region_stats(&reg_ext, out);
}

void *espw_obj_alloc(size_t n) {
    uint32_t m;
    void *p = NULL;

    if (n <= OBJ_BYTES) {
        m = tiku_esp32c61_mie_off();
        for (unsigned i = 0U; i < OBJ_SLOTS; i++) {
            if ((obj_used & (1ULL << i)) == 0U) {
                obj_used |= 1ULL << i;
                p = obj_pool[i];
                break;
            }
        }
        tiku_esp32c61_mie_restore(m);
    }
    if (p != NULL) {
        memset(p, 0, OBJ_BYTES);
        return p;
    }
    return espw_calloc(1U, n);
}

void espw_obj_free(void *p) {
    uintptr_t a = (uintptr_t)p, base = (uintptr_t)obj_pool;
    uint32_t m;

    if (a >= base && a < base + sizeof obj_pool) {
        m = tiku_esp32c61_mie_off();
        obj_used &= ~(1ULL << ((a - base) / OBJ_BYTES));
        tiku_esp32c61_mie_restore(m);
        return;
    }
    espw_free(p);
}
