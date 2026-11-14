/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * radio_memory.c - Word-width copies for S31 radio library hardware windows.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>

typedef uint32_t radio_word_t __attribute__((__may_alias__));

/** @brief Copy disjoint ranges; aligned words use 32-bit accesses, tails use bytes. */
void *memcpy(void *out, const void *in, size_t size)
{
    uint8_t *dst = out;
    const uint8_t *src = in;
    /* The vendor key-table writer calls memcpy for aligned 16-byte MMIO keys. */
    if (!(((uintptr_t)dst | (uintptr_t)src) & 3u)) {
        while (size >= 4) {
            *(volatile radio_word_t *)dst = *(const volatile radio_word_t *)src;
            dst += 4; src += 4; size -= 4;
        }
    }
    while (size--) *dst++ = *src++;
    return out;
}
