/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_c6_arch.h - Native C6 radio interrupt, timer and logging bindings.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WIFI_C6_ARCH_H
#define TIKU_WIFI_C6_ARCH_H
#include <arch/esp32c6/tiku_irq_arch.h>
#include <arch/esp32c6/tiku_trng_arch.h>
#include <stddef.h>
#include <stdint.h>
#define espw_arch_mie_off native_irq_save
#define espw_arch_mie_restore native_irq_restore
#define espw_arch_isr_t tiku_c6_isr_t
#define ESPW_ARCH_LINES_RADIO 4u
#define ESPW_ARCH_LINE_RADIO 24u
#define ESPW_ARCH_LEVEL_DEFAULT 3u
#define ESPW_ARCH_LEVEL_TIMER 2u
#define ESPW_ARCH_ALARM_DRIVER 2u
#define ESPW_ARCH_SRC_SYSTIMER(n) (57u + (n))
#define ESPW_ARCH_SYSTIMER_HZ 16000000u
#define ESPW_ARCH_F_MEPC 32u
#define ESPW_ARCH_F_RA 1u
#define ESPW_ARCH_XTAL_MHZ 40u
extern volatile uint8_t tiku_c6_in_trap;
#define espw_arch_in_isr() (tiku_c6_in_trap != 0)
/** @brief Claim a logical radio line and an exclusively owned physical route. */
void espw_arch_irq_attach(unsigned line, unsigned source, unsigned priority, tiku_c6_isr_t fn);
/** @brief Enable, disable or release the routes attached to a logical line. */
void espw_arch_irq_enable(unsigned line);
void espw_arch_irq_disable(unsigned line);
void espw_arch_irq_mark_flash(unsigned line, int mark);
/** @brief Read the native 16 MHz timer; a snapshot failure terminates the caller. */
uint64_t espw_arch_systimer(void);
/** @brief Arm or disarm the radio-owned SYSTIMER target2 alarm. */
void espw_arch_alarm_arm(unsigned alarm, uint64_t deadline);
void espw_arch_alarm_disarm(unsigned alarm);
/** @brief WFI leaves the radio domains powered; this hook changes no clocks. */
void espw_arch_sleep_hold(int hold);
/** @brief Return whether native boot established its checked flash mapping. */
int espw_arch_xip_ok(void);
/** @brief Append formatted foreground diagnostics to the RAM shell backend. */
int espw_c6_log(const char *format, ...);
/** @brief Stop execution on a radio ownership or hardware contract violation. */
void espw_c6_fatal(const char *message) __attribute__((noreturn));
/** @brief Fill bytes from RF-fed RNG while PHY is on, otherwise internal SAR. */
#define espw_c6_random tiku_trng_arch_read_bytes
/** @brief Restore owned digital modem fields after radio workers have stopped. */
void espw_c6_radio_release(void);
/** @brief Return nonzero while the C6 PHY owns the RF entropy source. */
int espw_c6_phy_active(void);
/** @brief Read the eFuse silicon revision as major times 100 plus minor. */
uint32_t espw_c6_revision(void);
/** @brief Pulse the controller-requested modem reset mask. */
void espw_c6_bt_reset(uint8_t modules, uint8_t start);
#endif
