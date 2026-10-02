/*
 * Tiku Drivers - ESP32-C61 radio libraries, their heap
 *
 * The libraries allocate and free at will; tikuOS has no malloc.  This is a
 * first-fit heap over one block the driver takes from the SRAM tier when the
 * radio comes up, and gives back once the radio is down and the heap empty.
 * Their locks, which they keep from one start to the next, sit apart in a
 * small static pool so that the heap can empty.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_ESP_HEAP_H_
#define TIKU_DRV_WIFI_ESP_HEAP_H_

#include <stddef.h>
#include <stdint.h>

/** @brief The heap's figures: what is free now, and the least it has been. */
typedef struct {
    uint32_t size;
    uint32_t free;
    uint32_t low;           /* the smallest free has been since init */
    uint32_t largest;       /* the largest free block now */
    uint32_t fails;         /* requests refused */
} espw_heap_stats_t;

/** @brief Take @p len bytes at @p base, 8-byte aligned, as the heap. */
void espw_heap_init(void *base, uint32_t len);

/** @brief Forget the heap: its memory goes back, nothing may be freed
 *         into it after. */
void espw_heap_reset(void);

/** @brief Bytes in use now. */
uint32_t espw_heap_used(void);

/** @brief @p n bytes, 8-byte aligned, or NULL.  Safe from an ISR. */
void *espw_malloc(size_t n);

/** @brief As espw_malloc(), zeroed. */
void *espw_calloc(size_t count, size_t n);

/** @brief Grow or shrink @p p to @p n bytes, moving it if need be. */
void *espw_realloc(void *p, size_t n);

/** @brief Give @p p back; NULL is ignored. */
void espw_free(void *p);

/** @brief The figures above. @param out  Filled */
void espw_heap_stats(espw_heap_stats_t *out);

/** @brief A small object (a lock) from the static pool, zeroed; from the
 *         heap when larger or the pool is full. */
void *espw_obj_alloc(size_t n);

/** @brief Give back what espw_obj_alloc() gave. */
void espw_obj_free(void *p);

#endif /* TIKU_DRV_WIFI_ESP_HEAP_H_ */
