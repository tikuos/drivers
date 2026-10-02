/*
 * Tiku Drivers - ESP32-C61 radio libraries, their heap
 *
 * Free blocks sit in one list in address order, so a free merges with both
 * neighbours.  A block in use carries its size and a magic word; freeing one
 * without it is reported, not obeyed.  Interrupts are masked inside, since
 * the libraries' interrupt handlers allocate too.
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

/** @brief A free block: its size (header included) and the next one. */
typedef struct espw_blk {
    uint32_t         size;
    struct espw_blk *next;
} espw_blk_t;

/* The lock pool: slots of 16 bytes, one bit each. */
#define OBJ_SLOTS   48U
#define OBJ_BYTES   16U

static uint32_t obj_pool[OBJ_SLOTS][OBJ_BYTES / 4U];
static uint64_t obj_used;

static espw_blk_t *heap_free_list;
static uint8_t    *heap_base;
static uint32_t    heap_size;
static uint32_t    heap_free_bytes;
static uint32_t    heap_low;
static uint32_t    heap_fails;

void espw_heap_init(void *base, uint32_t len) {
    uintptr_t b = ((uintptr_t)base + 7U) & ~(uintptr_t)7U;

    len -= (uint32_t)(b - (uintptr_t)base);
    len &= ~7UL;
    heap_base = (uint8_t *)b;
    heap_size = len;
    heap_free_list = (espw_blk_t *)b;
    heap_free_list->size = len;
    heap_free_list->next = NULL;
    heap_free_bytes = len;
    heap_low = len;
    heap_fails = 0U;
}

void espw_heap_reset(void) {
    uint32_t m = tiku_esp32c61_mie_off();

    heap_free_list = NULL;
    heap_base = NULL;
    heap_size = 0U;
    heap_free_bytes = 0U;
    tiku_esp32c61_mie_restore(m);
}

uint32_t espw_heap_used(void) {
    return heap_size - heap_free_bytes;
}

void *espw_malloc(size_t n) {
    uint32_t need, m;
    espw_blk_t **link, *b;
    uint32_t *hdr = NULL;

    if (n == 0U || n > heap_size) {
        return NULL;
    }
    need = ((uint32_t)n + 7U) & ~7UL;
    need += HDR;
    m = tiku_esp32c61_mie_off();
    for (link = &heap_free_list; (b = *link) != NULL; link = &b->next) {
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
        heap_free_bytes -= b->size;
        if (heap_free_bytes < heap_low) {
            heap_low = heap_free_bytes;
        }
        hdr = (uint32_t *)b;
        hdr[1] = USED_MAGIC;
        break;
    }
    if (hdr == NULL) {
        heap_fails++;
    }
    tiku_esp32c61_mie_restore(m);
    return hdr != NULL ? (uint8_t *)hdr + HDR : NULL;
}

void *espw_calloc(size_t count, size_t n) {
    size_t total = count * n;
    void *p;

    if (n != 0U && total / n != count) {
        return NULL;
    }
    p = espw_malloc(total);
    if (p != NULL) {
        memset(p, 0, total);
    }
    return p;
}

/** @brief The block @p p was handed out from, or NULL when it was not. */
static uint32_t *used_header(void *p) {
    uint32_t *hdr = (uint32_t *)((uint8_t *)p - HDR);

    if ((uint8_t *)hdr < heap_base ||
        (uint8_t *)hdr >= heap_base + heap_size ||
        ((uintptr_t)hdr & 7U) != 0U || hdr[1] != USED_MAGIC) {
        TIKU_PRINTF("[esp-wifi] free of 0x%08lx: not a heap block\n",
                    (unsigned long)(uintptr_t)p);
        return NULL;
    }
    return hdr;
}

void espw_free(void *p) {
    uint32_t *hdr;
    espw_blk_t *b, *prev = NULL, *cur;
    uint32_t m;

    if (p == NULL || (hdr = used_header(p)) == NULL) {
        return;
    }
    m = tiku_esp32c61_mie_off();
    b = (espw_blk_t *)hdr;
    hdr[1] = 0U;
    heap_free_bytes += b->size;
    for (cur = heap_free_list; cur != NULL && cur < b; cur = cur->next) {
        prev = cur;
    }
    /* Into the list in address order, merged with whichever neighbour
     * touches it. */
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
        heap_free_list = b;
    }
    tiku_esp32c61_mie_restore(m);
}

void *espw_realloc(void *p, size_t n) {
    uint32_t *hdr;
    uint32_t have;
    void *q;

    if (p == NULL) {
        return espw_malloc(n);
    }
    if (n == 0U) {
        espw_free(p);
        return NULL;
    }
    if ((hdr = used_header(p)) == NULL) {
        return NULL;
    }
    have = hdr[0] - HDR;
    if (n <= have) {
        return p;
    }
    q = espw_malloc(n);
    if (q != NULL) {
        memcpy(q, p, have);
        espw_free(p);
    }
    return q;
}

void espw_heap_stats(espw_heap_stats_t *out) {
    uint32_t m = tiku_esp32c61_mie_off();
    uint32_t big = 0U;

    for (espw_blk_t *b = heap_free_list; b != NULL; b = b->next) {
        if (b->size > big) {
            big = b->size;
        }
    }
    out->size = heap_size;
    out->free = heap_free_bytes;
    out->low = heap_low;
    out->largest = big > HDR ? big - HDR : 0U;
    out->fails = heap_fails;
    tiku_esp32c61_mie_restore(m);
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
