/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp_arch.h - target bindings used by the shared radio OS adapter.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP_RADIO_ARCH_H_
#define TIKU_ESP_RADIO_ARCH_H_
#if defined(PLATFORM_ESP32C5)
#include "c5/wifi_c5_arch.h"
#else
#include <arch/esp32c61/tiku_crt_early.h>
#include <arch/esp32c61/tiku_cpu_freq_boot_arch.h>
#include <arch/esp32c61/tiku_irq_arch.h>
#include <arch/esp32c61/tiku_sleep_arch.h>
#include <arch/esp32c61/tiku_timer_arch.h>
#include <arch/esp32c61/tiku_trng_arch.h>
#include <arch/esp32c61/tiku_xip_arch.h>
#include <arch/esp32c61/tiku_esp32c61_regs.h>
#define espw_arch_in_isr tiku_esp32c61_in_isr
#define espw_arch_mie_off tiku_esp32c61_mie_off
#define espw_arch_mie_restore tiku_esp32c61_mie_restore
#define espw_arch_irq_attach tiku_esp32c61_irq_attach
#define espw_arch_irq_enable tiku_esp32c61_irq_enable
#define espw_arch_irq_disable tiku_esp32c61_irq_disable
#define espw_arch_irq_mark_flash tiku_esp32c61_irq_mark_flash
#define espw_arch_alarm_arm tiku_esp32c61_alarm_arm
#define espw_arch_alarm_disarm tiku_esp32c61_alarm_disarm
#define espw_arch_systimer tiku_cpu_esp32c61_systimer
#define espw_arch_sleep_hold tiku_esp32c61_sleep_hold
#define espw_arch_xip_ok tiku_esp32c61_xip_ok
#define espw_arch_isr_t tiku_esp32c61_isr_t
#define ESPW_ARCH_LINES_RADIO TIKU_ESP32C61_LINES_RADIO
#define ESPW_ARCH_LINE_RADIO TIKU_ESP32C61_LINE_RADIO
#define ESPW_ARCH_LEVEL_DEFAULT TIKU_ESP32C61_LEVEL_DEFAULT
#define ESPW_ARCH_LEVEL_TIMER TIKU_ESP32C61_LEVEL_TIMER
#define ESPW_ARCH_ALARM_DRIVER TIKU_ESP32C61_ALARM_DRIVER
#define ESPW_ARCH_SRC_SYSTIMER ESP32C61_SRC_SYSTIMER
#define ESPW_ARCH_SYSTIMER_HZ ESP32C61_SYSTIMER_HZ
#define ESPW_ARCH_F_MEPC TIKU_ESP32C61_F_MEPC
#define ESPW_ARCH_F_RA TIKU_ESP32C61_F_RA
/* The DevKitC's crystal; the C5 reads its own. */
#define ESPW_ARCH_XTAL_MHZ 40u
#endif
#endif
