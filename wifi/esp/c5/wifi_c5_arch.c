/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_c5_arch.c - C5 radio CLIC ownership and dedicated SYSTIMER alarm.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_c5_arch.h"
#include "tiku_drv_phy_c5.h"
#include <arch/esp32c5/tiku_systimer_arch.h>
#include <arch/esp32c5/tiku_timer_arch.h>
#include <arch/esp32c5/tiku_xip_arch.h>
#include <arch/esp32c5/tiku_cpu_common.h>
#include <arch/esp32c5/tiku_esp32c5_regs.h>
#include <hal/tiku_printf_hal.h>

/* Four logical lines, with four additional physical lines for shared sources.
 */
static tiku_c5_isr_t owned[8];
static unsigned sources[8], groups[8];

void espw_arch_irq_attach(unsigned line, unsigned source, unsigned priority,
                          tiku_c5_isr_t fn)
{
    unsigned slot = line - ESPW_ARCH_LINE_RADIO, i;
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (slot >= 4 || !fn) {
        tiku_c5_fatal("radio IRQ logical line");
    }
    for (i = 0; i < 8; i++) {
        if (owned[i] && groups[i] == line && sources[i] == source) {
            if (!tiku_c5_irq_owned(ESPW_ARCH_LINE_RADIO + i, source, fn)) {
                tiku_c5_fatal("radio IRQ handler conflict");
            }
            TIKU_C5_IRQ_RESTORE(state);
            return;
        }
    }
    if (owned[slot]) {
        for (slot = 4; slot < 8 && owned[slot]; slot++) {
        }
    }
    if (slot == 8 ||
        tiku_c5_irq_attach(ESPW_ARCH_LINE_RADIO + slot, source, priority, fn)) {
        TIKU_PRINTF("[esp] IRQ logical line %u source %u priority %u\n", line,
                    source, priority);
        tiku_c5_fatal("radio IRQ ownership conflict");
    }
    owned[slot] = fn;
    sources[slot] = source;
    groups[slot] = line;
    TIKU_C5_IRQ_RESTORE(state);
}
void espw_arch_irq_enable(unsigned line)
{
    unsigned i, found = 0;
    for (i = 0; i < 8; i++) {
        if (owned[i] && groups[i] == line) {
            unsigned physical = ESPW_ARCH_LINE_RADIO + i;
            if (!tiku_c5_irq_owned(physical, sources[i], owned[i]) ||
                tiku_c5_irq_enable(physical, 1)) {
                tiku_c5_fatal("radio IRQ enable without ownership");
            }
            found = 1;
        }
    }
    if (!found) {
        tiku_c5_fatal("radio IRQ enable without handler");
    }
}
void espw_arch_irq_disable(unsigned line)
{
    unsigned i;
    for (i = 0; i < 8; i++) {
        if (owned[i] && groups[i] == line &&
            tiku_c5_irq_owned(ESPW_ARCH_LINE_RADIO + i, sources[i], owned[i])) {
            (void)tiku_c5_irq_enable(ESPW_ARCH_LINE_RADIO + i, 0);
        }
    }
}
void espw_arch_irq_mark_flash(unsigned line, int mark)
{
    unsigned i;
    for (i = 0; !mark && i < 8; i++) {
        if (owned[i] && groups[i] == line) {
            if (tiku_c5_irq_owned(ESPW_ARCH_LINE_RADIO + i, sources[i],
                                  owned[i])) {
                tiku_c5_irq_detach(ESPW_ARCH_LINE_RADIO + i);
            }
            owned[i] = NULL;
        }
    }
}
uint64_t espw_arch_systimer(void)
{
    uint64_t now;
    if (tiku_c5_systimer_read(&now)) {
        tiku_c5_fatal("radio timer read failed");
    }
    return now;
}
void espw_arch_alarm_disarm(unsigned alarm)
{
    uint32_t state;
    if (alarm != 2) {
        tiku_c5_fatal("radio alarm index");
    }
    if (!owned[0]) {
        return;
    }
    state = TIKU_C5_IRQ_SAVE();
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_CONF,
                      TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_CONF) &
                          ~TIKU_C5_SYSTIMER_TARGET(2));
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_ENA,
                      TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_INT_ENA) & ~4u);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_CLR, 4u);
    TIKU_C5_IRQ_RESTORE(state);
}
void espw_arch_alarm_arm(unsigned alarm, uint64_t deadline)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    uint64_t now = espw_arch_systimer();
    if (alarm != 2 || !owned[0]) {
        tiku_c5_fatal("radio alarm without ownership");
    }
    if ((int64_t)(deadline - now) < 64) {
        deadline = now + 64;
    }
    deadline &= TIKU_C5_SYSTIMER_MASK;
    espw_arch_alarm_disarm(2);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_CFG(2), 0);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_HI(2),
                      (uint32_t)(deadline >> 32));
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_LO(2), (uint32_t)deadline);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_LOAD(2), 1);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_ENA,
                      TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_INT_ENA) | 4u);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_CONF,
                      TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_CONF) |
                          TIKU_C5_SYSTIMER_TARGET(2));
    TIKU_C5_IRQ_RESTORE(state);
}
void espw_arch_sleep_hold(int hold)
{
    (void)hold;
}
int espw_arch_xip_ok(void)
{
    tiku_c5_xip_require();
    return 1;
}

int espw_c5_random(uint8_t *out, size_t length)
{
    /* The platform TRNG reads the RF-fed RNG register while the PHY owns
     * the analog bus, and the SAR source otherwise. */
    return tiku_trng_arch_read_bytes(out, length);
}
