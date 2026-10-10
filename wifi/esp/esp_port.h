/*
 * Tiku Drivers - ESP32-C61 radio, what the driver's pieces share
 *
 * esp_core.c is what both radios stand on, esp_osi.c the OS the libraries
 * see, esp_phy.c the modem's clocks and the PHY; tiku_drv_wifi_esp.c and
 * esp_ble.c are the radios' life cycles.  Internal: nothing outside
 * drivers/wifi/esp includes this.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_WIFI_ESP_PORT_H_
#define TIKU_DRV_WIFI_ESP_PORT_H_

#include <stddef.h>
#include <stdint.h>

#include <hal/tiku_printf_hal.h>
#include <kernel/threads/tiku_thread.h>
#include <kernel/timers/tiku_clock.h>
#include "esp_abi.h"

#define ESPW_PRINTF(...) TIKU_PRINTF("[esp-wifi] " __VA_ARGS__)
#define ESPB_PRINTF(...) TIKU_PRINTF("[esp-ble] " __VA_ARGS__)

/* esp_core.c --------------------------------------------------------------*/

/* The radios, as the core counts them. */
#define ESPW_RADIO_WIFI     (1U << 0)
#define ESPW_RADIO_BLE      (1U << 1)
#define ESPW_RADIO_154      (1U << 2)

/* BLE built with Wi-Fi or with the C5's 15.4 MAC: they may run at once,
 * under the coexistence arbiter. */
#define ESPW_COEX   ((TIKU_DRV_BLE_ESP_ENABLE + 0) && \
                     ((TIKU_DRV_WIFI_ESP_ENABLE + 0) || \
                      (TIKU_DRV_154_C5_ENABLE + 0)))

/**
 * @brief What a radio stands on, brought up with the first: the libraries'
 *        SRAM heap (@p heap_bytes, or one shared size with both radios
 *        built), the modem's gating, the timer service, the arbiter; and
 *        @p ext_bytes of PSRAM for packet buffers (0: none).  Kernel thread
 *        only.  @return 0, or -1 (said why)
 */
int espw_core_up(uint8_t radio, uint32_t heap_bytes, uint32_t ext_bytes);

/** @brief The radio is down: the last one stops the timer service and gives
 *         the heap back if nothing is left in it. */
void espw_core_down(uint8_t radio);

/** @brief The arbiter's adapter registered and pre-initialised, once per
 *         boot (esp_coex.c; both radios built). */
void espw_coex_start(void);

/** @brief The radios up now (ESPW_RADIO_*). */
uint8_t espw_core_radios(void);

/* esp_osi.c ---------------------------------------------------------------*/

/** @brief A wait's end, set once so a woken waiter does not start over. */
typedef struct {
    uint8_t           forever;
    tiku_clock_time_t until;
} espw_deadline_t;

/** @brief A deadline @p ms from now; OSI_FUNCS_TIME_BLOCKING has none. */
void espw_deadline_start(espw_deadline_t *d, uint32_t ms);

/**
 * @brief Block on @p q once, inside one atomic section; the caller tests its
 *        condition again after.  A task deleted meanwhile ends here.
 *        @return 0 once nothing more may be waited: a zero timeout, an
 *        interrupt handler, or the deadline gone by
 */
int espw_deadline_wait(tiku_waitq_t *q, espw_deadline_t *d, uint32_t ms);

/** @brief The running thread as a lock owner: a worker, or the kernel. */
void *espw_self(void);

/** @brief Sleep @p ms; 0 yields. */
void espw_delay_ms(uint32_t ms);

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

/** @brief The BLE MAC's clocks on (its resets pulsed, its sleep clock the
 *         crystal / 400), or off. */
void espw_modem_bt_on(void);
void espw_modem_bt_off(void);

/** @brief The BLE timer's sleep clock, in Hz. */
uint32_t espw_modem_bt_lp_hz(void);

/** @brief The 15.4 MAC's clocks and gate map on, or off (ESP32-C5). */
void espw_modem_154_on(void);
void espw_modem_154_off(void);

/** @brief The PHY on for Wi-Fi (calibrating it the first time) or off; it
 *         powers down after the last radio's off. */
void espw_phy_enable(void);
void espw_phy_disable(void);

/** @brief The same for BLE. */
void espw_phy_bt_enable(void);
void espw_phy_bt_disable(void);

/** @brief The same for the 15.4 MAC (ESP32-C5). */
void espw_phy_154_enable(void);
void espw_phy_154_disable(void);

/** @brief The baseband BLE and 15.4 share, initialised by its first user
 *         and left as it is by the rest. */
void espw_btbb_enable(void);
void espw_btbb_disable(void);

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

/** @brief The PMK of the network about to be joined, derived by the driver
 *         from its passphrase; NULL wipes it. */
void espw_wpa_set_pmk(const uint8_t *pmk);

/* esp_crypto.c ------------------------------------------------------------*/

/** @brief Fill the crypto table esp_wifi_init_internal() takes. */
void espw_crypto_table(wpa_crypto_funcs_t *t);

/* tiku_drv_wifi_esp.c -----------------------------------------------------*/

/** @brief An event the libraries posted. */
void espw_event(const char *base, int32_t id, const void *data, size_t len);

#endif /* TIKU_DRV_WIFI_ESP_PORT_H_ */
