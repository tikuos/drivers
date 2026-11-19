/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_s31_arch.h - S31 radio interrupt, timer and console bindings.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WIFI_S31_ARCH_H_
#define TIKU_WIFI_S31_ARCH_H_
#include <arch/esp32s31/tiku_runtime_arch.h>
#include <arch/esp32s31/tiku_timer_arch.h>
#include <stddef.h>
#include <stdint.h>
typedef void (*espw_arch_isr_t)(void);
#define ESPW_ARCH_LINES_RADIO 4u
#define ESPW_ARCH_LINE_RADIO 20u
#define ESPW_ARCH_LEVEL_DEFAULT 3u
#define ESPW_ARCH_LEVEL_TIMER 2u
#define ESPW_ARCH_ALARM_DRIVER 2u
#define ESPW_ARCH_SRC_SYSTIMER(n) (33u + (n))
#define ESPW_ARCH_SYSTIMER_HZ 16000000u
#define ESPW_ARCH_F_MEPC (S31_FRAME_PC / 4)
#define ESPW_ARCH_F_RA 1u
#define ESPW_ARCH_XTAL_MHZ 40u
#define espw_arch_systimer tiku_s31_time
/** @brief Mask machine interrupts and return their previous status. */
static inline uint32_t espw_arch_mie_off(void)
{
    uint32_t state;
    __asm__ volatile("csrrci %0,mstatus,8" : "=r"(state) :: "memory");
    return state;
}
/** @brief Restore the saved machine interrupt enable bit. */
static inline void espw_arch_mie_restore(uint32_t state)
{ if (state & 8u) __asm__ volatile("csrsi mstatus,8" ::: "memory"); }
/** @brief Report whether a machine trap is being dispatched. */
static inline int espw_arch_in_isr(void) { return tiku_s31_in_trap; }
/** @brief Claim a radio source on a logical line; halt on an ownership conflict. */
void espw_arch_irq_attach(unsigned line, unsigned source, unsigned priority, espw_arch_isr_t fn);
/** @brief Enable or disable every source assigned to a logical line. */
void espw_arch_irq_enable(unsigned line);
void espw_arch_irq_disable(unsigned line);
/** @brief Release a logical line when mark is zero; flash writes are unavailable. */
void espw_arch_irq_mark_flash(unsigned line, int mark);
/** @brief Arm or disarm comparator two, with ownership validation. */
void espw_arch_alarm_arm(unsigned alarm, uint64_t deadline);
void espw_arch_alarm_disarm(unsigned alarm);
/** @brief Retain the active clock configuration; scheduler idle uses WFI. */
void espw_arch_sleep_hold(int hold);
/** @brief Format at most 191 bytes on UART0; refuse output from a handler. */
void espw_s31_log(const char *format, ...);
/** @brief Read from the initialized LP random source with hardware health checks. */
int espw_s31_random(uint8_t *out, size_t length);
#ifndef TIKU_S31_RADIO_IMAGE_BOUNDS_ENABLE
/** @brief Admit station entries using the linked radio image bounds. */
#define TIKU_S31_RADIO_IMAGE_BOUNDS_ENABLE 0
#endif
/** @brief Check that the station entry is in the bootloader-mapped radio image. */
int espw_arch_xip_ok(void);
/** @brief Reserve RF and Wi-Fi DMA access before creating workers; return 0 or -1. */
int espw_s31_radio_prepare(void);
/** @brief Restore PHY and DMA ownership after the vendor workers stop. */
void espw_s31_radio_release(void);
#endif
