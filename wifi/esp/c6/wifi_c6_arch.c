/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_c6_arch.c - C6 PLIC radio ownership and dedicated SYSTIMER target2.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_c6_arch.h"
#include "../esp_heap.h"
#include <arch/esp32c6/tiku_flash_arch.h>
#include <shell/tiku_shell_io.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static tiku_c6_isr_t owned[8];
static unsigned sources[8], groups[8];
/** @brief Return direct vendor libc frees to the radio allocator. */
void __wrap_free(void *pointer)
{
    espw_free(pointer);
}
void espw_c6_fatal(const char *message)
{
    (void)espw_c6_log("[esp-c6] %s\n", message);
    __builtin_trap();
    for (;;) {
    }
}
int espw_c6_log(const char *format, ...)
{
    char line[160];
    va_list args;
    if (espw_arch_in_isr())
        return 0;
    va_start(args, format);
    int result = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    uint32_t state = native_irq_save();
    for (unsigned i = 0; line[i]; i++)
        tiku_shell_io_putc(line[i]);
    native_irq_restore(state);
    return result;
}
void espw_arch_irq_attach(unsigned line, unsigned source, unsigned priority, tiku_c6_isr_t fn)
{
    unsigned slot = line - ESPW_ARCH_LINE_RADIO;
    uint32_t state = native_irq_save();
    if (slot >= 4 || !fn)
        espw_c6_fatal("radio logical interrupt");
    for (unsigned i = 0; i < 8; i++) {
        if (owned[i] && groups[i] == line && sources[i] == source) {
            if (!tiku_c6_irq_owned(24 + i, source, fn))
                espw_c6_fatal("radio interrupt changed");
            native_irq_restore(state);
            return;
        }
    }
    if (owned[slot])
        for (slot = 4; slot < 8 && owned[slot]; slot++) {
        }
    if (slot == 8 || tiku_c6_irq_attach(24 + slot, source, priority, fn))
        espw_c6_fatal("radio interrupt ownership");
    owned[slot] = fn;
    sources[slot] = source;
    groups[slot] = line;
    native_irq_restore(state);
}
void espw_arch_irq_enable(unsigned line)
{
    unsigned found = 0;
    for (unsigned i = 0; i < 8; i++)
        if (owned[i] && groups[i] == line) {
            if (!tiku_c6_irq_owned(24 + i, sources[i], owned[i]) || tiku_c6_irq_enable(24 + i, 1))
                espw_c6_fatal("radio interrupt enable");
            found = 1;
        }
    if (!found)
        espw_c6_fatal("radio interrupt missing");
}
void espw_arch_irq_disable(unsigned line)
{
    for (unsigned i = 0; i < 8; i++)
        if (owned[i] && groups[i] == line && tiku_c6_irq_owned(24 + i, sources[i], owned[i]))
            (void)tiku_c6_irq_enable(24 + i, 0);
}
void espw_arch_irq_mark_flash(unsigned line, int mark)
{
    for (unsigned i = 0; !mark && i < 8; i++)
        if (owned[i] && groups[i] == line) {
            if (tiku_c6_irq_owned(24 + i, sources[i], owned[i]))
                tiku_c6_irq_detach(24 + i);
            owned[i] = NULL;
        }
}
uint64_t espw_arch_systimer(void)
{
    uint64_t value;
    if (tiku_c6_systimer_read(&value))
        espw_c6_fatal("radio timer snapshot");
    return value;
}
void espw_arch_alarm_disarm(unsigned alarm)
{
    if (alarm != 2)
        espw_c6_fatal("radio alarm index");
    if (!owned[0])
        return;
    uint32_t state = native_irq_save();
    C6_REG(C6_SYSTIMER) &= ~(1u << 22);
    C6_REG(C6_SYSTIMER + 0x64) &= ~4u;
    C6_REG(C6_SYSTIMER + 0x6c) = 4u;
    native_irq_restore(state);
}
void espw_arch_alarm_arm(unsigned alarm, uint64_t deadline)
{
    uint32_t state = native_irq_save();
    uint64_t now = espw_arch_systimer();
    if (alarm != 2 || !owned[0])
        espw_c6_fatal("radio alarm ownership");
    if ((int64_t)(deadline - now) < 64)
        deadline = now + 64;
    deadline &= (1ULL << 52) - 1;
    espw_arch_alarm_disarm(2);
    C6_REG(C6_SYSTIMER + 0x3c) = 0;
    C6_REG(C6_SYSTIMER + 0x2c) = (uint32_t)(deadline >> 32);
    C6_REG(C6_SYSTIMER + 0x30) = (uint32_t)deadline;
    C6_REG(C6_SYSTIMER + 0x58) = 1;
    C6_REG(C6_SYSTIMER + 0x64) |= 4u;
    C6_REG(C6_SYSTIMER) |= 1u << 22;
    native_irq_restore(state);
}
void espw_arch_sleep_hold(int hold)
{
    (void)hold;
}
int espw_arch_xip_ok(void)
{
    return tiku_c6_flash_jedec() != 0;
}
