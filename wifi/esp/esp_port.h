/*
 * Tiku Drivers - ESP32-C61 radio, what the driver's pieces share
 *
 * esp_osi.c is the OS the libraries see, esp_phy.c the modem's clocks and
 * the PHY, tiku_drv_wifi_esp.c the radio's life cycle.  Internal: nothing
 * outside drivers/wifi/esp includes this.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_ESP_PORT_H_
#define TIKU_DRV_WIFI_ESP_PORT_H_

#include <stddef.h>
#include <stdint.h>

#include <hal/tiku_printf_hal.h>
#include "esp_abi.h"

#define ESPW_PRINTF(...) TIKU_PRINTF("[esp-wifi] " __VA_ARGS__)

/* esp_osi.c ---------------------------------------------------------------*/

/** @brief The table handed to esp_wifi_init_internal(). */
extern wifi_osi_funcs_t espw_osi_funcs;

/** @brief Start what the table needs beneath it: the timer service's alarm
 *         and thread.  Kernel thread only.  @return 0, or -1 */
int espw_osi_start(void);

/** @brief Stop the timer service, quiet the radio's lines, and see every
 *         task end.  @return 0, or -1 when one did not: keep the heap */
int espw_osi_stop(void);

/** @brief The libraries' timer calls, for the driver's own timers. */
void espw_timer_setfn(void *ptimer, void *fn, void *arg);
void espw_timer_arm_us(void *ptimer, uint32_t us, bool repeat);
void espw_timer_disarm(void *ptimer);

/** @brief The radio's interrupts taken since boot. */
uint32_t espw_irq_count(void);

/** @brief Microseconds since boot, as the libraries count time. */
int64_t espw_time_us(void);

/* esp_phy.c ---------------------------------------------------------------*/

/** @brief The modem's clock gating, low-power clock and Wi-Fi clocks off. */
void espw_modem_init(void);

/** @brief The Wi-Fi MAC and baseband clocks on, or off. */
void espw_modem_wifi_clock_on(void);
void espw_modem_wifi_clock_off(void);

/** @brief Pulse the Wi-Fi MAC's reset. */
void espw_modem_wifi_reset(void);

/** @brief Whether the stack is up: its clocks then stay on through PHY
 *         restarts, as the MAC keeps state behind them. */
void espw_modem_wifi_inited(int on);

/** @brief The PHY on (calibrating it the first time) or off. */
void espw_phy_enable(void);
void espw_phy_disable(void);

/** @brief The last calibration: its result and how long it took, and
 *         whether it ran since the last ask (a restart wakes the PHY). */
int espw_phy_cal_result(uint32_t *us, int *fresh);

/** @brief A MAC address of @p type (esp_mac_type_t). @return ESP_OK */
int espw_read_mac(uint8_t *mac, unsigned int type);

/* esp_wpa.c ---------------------------------------------------------------*/

/** @brief Hand the stack its supplicant; it consults one even to scan.
 *         @return 0, or -1 without memory */
int espw_wpa_register(void);

/** @brief Take the supplicant back (the stack frees its table). */
void espw_wpa_unregister(void);

/* tiku_drv_wifi_esp.c -----------------------------------------------------*/

/** @brief An event the libraries posted. */
void espw_event(const char *base, int32_t id, const void *data, size_t len);

#endif /* TIKU_DRV_WIFI_ESP_PORT_H_ */
