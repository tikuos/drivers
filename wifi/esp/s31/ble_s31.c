/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * ble_s31.c - S31 controller ownership and bounded HCI transport.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_bt.h"
#include "esp_hci_driver.h"
#include "esp_hci_internal.h"
#include "ble_mbuf.h"
#include "../esp_heap.h"
#include <interfaces/bluetooth/tiku_bt.h>
#include <interfaces/bluetooth/tiku_bt_transport.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/threads/tiku_thread.h>
#include <arch/esp32s31/tiku_runtime_arch.h>
#include <stddef.h>
#include <string.h>

#define BLE_RADIO 2u
#define RX_BYTES 4096u
#define PACKET_BYTES 260u
extern int espw_core_up(uint8_t radio, uint32_t bytes, uint32_t external);
extern void espw_core_down(uint8_t radio);
extern int espw_core_idle(void);
extern void espw_delay_ms(uint32_t ms);
extern const char *ble_controller_get_compile_version(void);
_Static_assert(offsetof(esp_bt_ctrl_le_config_t, config_magic) == 116,
               "S31 controller configuration ABI");
_Static_assert(sizeof(esp_bt_controller_config_t) == 200,
               "S31 common controller configuration ABI");

static uint8_t ring[RX_BYTES];
static unsigned head, used, dropped, active, owned, changing;
static hci_driver_forward_fn *forward;
static tiku_waitq_t rx_wait;

/** @brief Append bytes while the caller holds the interrupt guard. */
static void append(const uint8_t *data, unsigned length)
{
    for (unsigned i = 0; i < length; i++)
        ring[(head + used + i) % RX_BYTES] = data[i];
    used += length;
}

/** @brief Copy one controller packet and release the controller-owned buffer. */
static int controller_rx(hci_driver_data_type_t type, uint8_t *data,
                         uint32_t length, hci_driver_direction_t direction)
{
    uint8_t packet[PACKET_BYTES];
    int valid = length < sizeof packet;
    packet[0] = type;
    if (direction == HCI_DRIVER_DIR_LEC2H) {
        if (type == HCI_DRIVER_TYPE_ACL) {
            struct ble_mbuf *buffer = (struct ble_mbuf *)data;
            if (valid) valid = ble_mbuf_copydata(buffer, 0, length, packet + 1) == 0;
            ble_mbuf_free_chain(buffer);
        } else if (type == HCI_DRIVER_TYPE_EVT) {
            if (valid) memcpy(packet + 1, data, length);
            r_ble_hci_trans_buf_free(data);
        } else {
            valid = 0;
        }
    } else if (direction == HCI_DRIVER_DIR_BTDMC2H && type == HCI_DRIVER_TYPE_EVT) {
        hci_driver_packet_t *buffer = (hci_driver_packet_t *)data;
        if (valid) memcpy(packet + 1, buffer->data, length);
        btdm_hci_trans_buf_free(buffer);
    } else {
        valid = 0;
    }
    tiku_atomic_enter();
    if (!valid || length + 3u > RX_BYTES - used) {
        if (dropped != UINT32_MAX) dropped++;
    } else {
        uint16_t size = length + 1u;
        uint8_t bytes[2] = {size, size >> 8};
        append(bytes, 2);
        append(packet, size);
    }
    tiku_thread_wake_all(&rx_wait);
    tiku_atomic_exit();
    return 0;
}

/** @brief Attach the controller forwarding function and clear receive state. */
static int transport_init(hci_driver_forward_fn *callback)
{
    head = used = dropped = 0;
    forward = callback;
    return callback ? 0 : -1;
}

/** @brief Detach the transport after the controller has stopped. */
static void transport_deinit(void)
{
    forward = NULL;
    head = used = 0;
}

hci_driver_ops_t hci_driver_vhci_ops = {
    controller_rx, transport_init, transport_deinit
};

/** @brief Validate complete HCI commands and BLE ACL packets before allocation. */
static int send_packet(const uint8_t *packet, uint16_t length)
{
    if (!active || !forward) return TIKU_DRV_ERR_NOT_PRESENT;
    if (dropped) return TIKU_DRV_ERR_INIT;
    if (!packet || length < 4 || length > PACKET_BYTES)
        return TIKU_DRV_ERR_INVALID;
    if (packet[0] == HCI_DRIVER_TYPE_CMD && length == 4u + packet[3]) {
        hci_driver_packet_t *buffer = btdm_hci_trans_buf_alloc(HCI_DRIVER_TYPE_CMD, 0);
        if (!buffer) return TIKU_DRV_ERR_TIMEOUT;
        memcpy(buffer->data, packet + 1, length - 1u);
        buffer->length = length - 1u;
        return forward(HCI_DRIVER_TYPE_CMD, (uint8_t *)buffer, length - 1u,
                       HCI_DRIVER_DIR_H2C, HCI_DRIVER_CMD);
    }
    if (packet[0] == HCI_DRIVER_TYPE_ACL && length >= 5 &&
        length == 5u + packet[3] + 256u * packet[4]) {
        uint16_t flags = packet[1] | ((uint16_t)packet[2] << 8);
        uint16_t handle = flags & HCI_INTERNAL_CONN_MASK;
        if ((flags & 0xc000u) || !HCI_INTERNAL_CONN_IS_BLE_ACL(handle))
            return TIKU_DRV_ERR_INVALID;
        struct ble_mbuf *buffer = ble_msys_get_pkthdr(length - 1u,
                                             ESP_HCI_INTERNAL_ACL_MBUF_LEADINGSPCAE);
        if (!buffer) return TIKU_DRV_ERR_TIMEOUT;
        if (ble_mbuf_append(buffer, packet + 1, length - 1u)) {
            ble_mbuf_free_chain(buffer);
            return TIKU_DRV_ERR_TIMEOUT;
        }
        return forward(HCI_DRIVER_TYPE_ACL, (uint8_t *)buffer, length - 1u,
                       HCI_DRIVER_DIR_H2C, HCI_DRIVER_LE_ACL);
    }
    return TIKU_DRV_ERR_INVALID;
}

/** @brief Read a queued packet; insufficient output capacity leaves it queued. */
static int receive_packet(uint8_t *out, uint16_t capacity)
{
    int result = 0;
    if (!out || capacity < 2) return TIKU_DRV_ERR_INVALID;
    tiku_atomic_enter();
    if (dropped) result = TIKU_DRV_ERR_INIT;
    else if (used) {
        unsigned length = ring[head] | ((unsigned)ring[(head + 1) % RX_BYTES] << 8);
        if (length > capacity) result = TIKU_DRV_ERR_INVALID;
        else {
            head = (head + 2) % RX_BYTES;
            for (unsigned i = 0; i < length; i++) out[i] = ring[(head + i) % RX_BYTES];
            head = (head + length) % RX_BYTES;
            used -= length + 2;
            result = length;
        }
    }
    tiku_atomic_exit();
    return result;
}

/** @brief Yield the kernel thread while the controller processes HCI. */
static void wait_packet(uint16_t ms)
{
    unsigned long ticks = ((unsigned long)ms * TIKU_CLOCK_SECOND + 999u) / 1000u;
    tiku_atomic_enter();
    if (!used && !dropped) (void)tiku_thread_wait(&rx_wait, ticks ? ticks : 1);
    tiku_atomic_exit();
}

/** @brief Report controller readiness independently of host initialization. */
static int is_ready(void) { return active && !dropped; }
static const tiku_bt_transport_t transport = {
    send_packet, receive_packet, is_ready, wait_packet, ble_controller_get_compile_version
};

/** @brief Stop the host, controller and shared radio resources in ownership order. */
static int power_down(void)
{
    if (active) tiku_bt_shutdown();
    active = 0;
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED &&
        esp_bt_controller_disable()) return TIKU_DRV_ERR_INIT;
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED &&
        esp_bt_controller_deinit()) return TIKU_DRV_ERR_INIT;
    espw_core_down(BLE_RADIO);
    dropped = 0;
    if (!espw_core_idle()) return TIKU_DRV_ERR_INIT;
    owned = 0;
    return 0;
}

int tiku_bt_controller_power(uint8_t on)
{
    int result;
    if (changing || tiku_s31_in_trap || !tiku_thread_in_kernel())
        return TIKU_DRV_ERR_TIMEOUT;
    if ((!on && !owned) || (on && active && !dropped)) return 0;
    changing = 1;
    if (!on) result = power_down();
    else if (!espw_core_idle()) result = TIKU_DRV_ERR_TIMEOUT;
    else {
        esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        result = espw_core_up(BLE_RADIO, 56u * 1024u, 0);
        if (!result) {
            owned = 1;
            result = esp_bt_controller_init(&config);
            if (!result) result = esp_bt_controller_enable(ESP_BT_MODE_BLE);
            if (!result) {
                active = 1;
                espw_delay_ms(100);
                result = tiku_bt_register_transport(&transport);
                if (!result) result = tiku_bt_init();
            }
            if (result) (void)power_down();
        }
        if (result) result = TIKU_DRV_ERR_INIT;
    }
    changing = 0;
    return result;
}
