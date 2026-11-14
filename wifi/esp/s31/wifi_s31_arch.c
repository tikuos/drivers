/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_s31_arch.c - S31 radio source ownership and driver alarm.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_s31_arch.h"
#include <arch/esp32s31/tiku_trng_arch.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
static struct { espw_arch_isr_t fn; unsigned source, group; } owned[8];
void espw_arch_irq_attach(unsigned line, unsigned source, unsigned priority, espw_arch_isr_t fn)
{
    if (line < 20 || line > 23 || !fn) tiku_s31_fatal("radio IRQ line");
    unsigned slot = line - 20;
    for (unsigned i = 0; i < 8; i++) {
        if (owned[i].fn && owned[i].group == line && owned[i].source == source) {
            if (owned[i].fn != fn) tiku_s31_fatal("radio IRQ handler conflict");
            return;
        }
    }
    if (owned[slot].fn) for (slot = 4; slot < 8 && owned[slot].fn; slot++) { }
    if (slot == 8 || tiku_s31_irq_claim(20 + slot, source, priority, fn))
        tiku_s31_fatal("radio IRQ claim");
    owned[slot].fn = fn; owned[slot].source = source; owned[slot].group = line;
}
void espw_arch_irq_enable(unsigned line)
{
    unsigned count = 0;
    for (unsigned i = 0; i < 8; i++) if (owned[i].fn && owned[i].group == line) {
        if (tiku_s31_irq_enable(20 + i, 1)) tiku_s31_fatal("radio IRQ enable");
        count++;
    }
    if (!count) tiku_s31_fatal("radio IRQ unowned");
}
void espw_arch_irq_disable(unsigned line)
{
    for (unsigned i = 0; i < 8; i++) if (owned[i].fn && owned[i].group == line)
        if (tiku_s31_irq_enable(20 + i, 0)) tiku_s31_fatal("radio IRQ disable");
}
void espw_arch_irq_mark_flash(unsigned line, int mark)
{
    for (unsigned i = 0; !mark && i < 8; i++) if (owned[i].fn && owned[i].group == line) {
        if (tiku_s31_irq_release(20 + i, owned[i].fn)) tiku_s31_fatal("radio IRQ release");
        owned[i].fn = NULL;
    }
}
void espw_arch_alarm_arm(unsigned alarm, uint64_t at)
{
    if (alarm != 2 || tiku_s31_driver_alarm(at, 1)) tiku_s31_fatal("radio alarm arm");
}
void espw_arch_alarm_disarm(unsigned alarm)
{
    if (alarm != 2 || tiku_s31_driver_alarm(0, 0)) tiku_s31_fatal("radio alarm disarm");
}
void espw_arch_sleep_hold(int hold) { (void)hold; }
void espw_s31_log(const char *format, ...)
{
    if (tiku_s31_in_trap) return;
    char line[192]; va_list args;
    va_start(args, format); (void)vsnprintf(line, sizeof line, format, args); va_end(args);
    tiku_s31_puts(line);
}
int espw_s31_random(uint8_t *out, size_t length)
{ return tiku_trng_arch_read_bytes(out, length); }

/** @brief Stop an aborted radio operation with its interrupt state masked. */
void abort(void) { tiku_s31_fatal("radio abort"); }
/** @brief Refuse a second libc heap; radio allocation uses espw_heap. */
void *_sbrk(ptrdiff_t increment)
{ (void)increment; errno = ENOMEM; return (void *)-1; }
