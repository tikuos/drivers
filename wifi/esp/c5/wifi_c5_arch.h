/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_c5_arch.h - C5 radio interrupt, timer, cache and clock bindings.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WIFI_C5_ARCH_H_
#define TIKU_WIFI_C5_ARCH_H_
#include <arch/esp32c5/tiku_irq_arch.h>
#include <arch/esp32c5/tiku_timer_arch.h>
#include <arch/esp32c5/tiku_trng_arch.h>
#include <stdint.h>
#include <stddef.h>
#define espw_arch_mie_off         TIKU_C5_IRQ_SAVE
#define espw_arch_mie_restore     TIKU_C5_IRQ_RESTORE
#define espw_arch_isr_t           tiku_c5_isr_t
#define ESPW_ARCH_LINES_RADIO     4u
#define ESPW_ARCH_LINE_RADIO      4u
#define ESPW_ARCH_LEVEL_DEFAULT   3u
#define ESPW_ARCH_LEVEL_TIMER     2u
#define ESPW_ARCH_ALARM_DRIVER    2u
#define ESPW_ARCH_SRC_SYSTIMER(n) (61u + (n))
#define ESPW_ARCH_SYSTIMER_HZ     16000000u
#define ESPW_ARCH_F_MEPC          (TIKU_C5_FRAME_PC / 4)
#define ESPW_ARCH_F_RA            1u
#define espw_arch_in_isr          tiku_c5_in_isr

/** @brief Claim a C5 radio line; an ownership conflict terminates startup. */
void espw_arch_irq_attach(unsigned line, unsigned source, unsigned priority,
                          tiku_c5_isr_t fn);
/** @brief Enable or disable a claimed radio interrupt. */
void espw_arch_irq_enable(unsigned line);
void espw_arch_irq_disable(unsigned line);
/** @brief Release an owned line when mark is zero; flash writes globally mask
 * interrupts. */
void espw_arch_irq_mark_flash(unsigned line, int mark);
/** @brief Read SYSTIMER counts, halting on a hardware read failure. */
uint64_t espw_arch_systimer(void);
/** @brief Arm or disarm the exclusively owned third SYSTIMER alarm. */
void espw_arch_alarm_arm(unsigned alarm, uint64_t deadline);
void espw_arch_alarm_disarm(unsigned alarm);
/** @brief C5 scheduler idle is WFI and does not gate the radio's clocks. */
void espw_arch_sleep_hold(int hold);
/** @brief The C5 boot validates its XIP companion before driver registration.
 */
int espw_arch_xip_ok(void);
/** @brief Calibrate and reserve RF before creating vendor workers. */
int espw_c5_radio_prepare(void);
/** @brief Close RF after vendor workers have stopped. */
void espw_c5_radio_release(void);
/** @brief Random bytes from the platform TRNG, RF-fed while the PHY is on. */
int espw_c5_random(uint8_t *out, size_t length);
#endif
