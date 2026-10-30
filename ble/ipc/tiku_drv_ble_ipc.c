/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_drv_ble_ipc.c - P4 Bluetooth host transport over a leased C6 mailbox.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_watchdog_hal.h>
#include <interfaces/bluetooth/tiku_bt.h>
#include <interfaces/bluetooth/tiku_bt_transport.h>
#include <kernel/drivers/tiku_drv.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/timers/tiku_timer.h>
#include <services/ipc/tiku_ipc_ble.h>
#include <stdio.h>
#include <string.h>
TIKU_PROCESS(ble_client, "P4 BLE IPC");
static tiku_ipc_wire_t *wire;
static tiku_ipc_endpoint_t endpoint;
static tiku_ble_sender_t sender;
static tiku_ble_packets_t incoming;
static struct tiku_timer timer;
static uint32_t session, serial, claim, peer_build, faults;
static uint8_t available, powered, desired, busy, op, request[17], request_length;
static int result;
static tiku_clock_time_t deadline, retry, renew;
/** @brief Drop host admission immediately; the service's lease expires on the peer. */
static void offline(uint8_t fault)
{
    uint8_t was_powered = powered;
    powered = 0;
    if (was_powered)
        tiku_bt_shutdown();
    claim = 0;
    available = 0;
    desired = op = 0;
    result = TIKU_DRV_ERR_NOT_PRESENT;
    tiku_ble_sender_cancel(&sender, endpoint);
    tiku_radio_wipe(&incoming, sizeof incoming);
    faults += fault;
}
/** @brief Queue complete controller packets only for the current powered claim. */
static tiku_ipc_err_t consume(void *unused, const void *data, uint16_t length)
{
    const uint8_t *p = data;
    (void)unused;
    if (!claim || !wire || !wire->ready || session != wire->local_session || length < 4 ||
        tiku_radio_get32(p) != claim)
        return TIKU_IPC_DOWN;
    if (!tiku_ble_packet_valid(p + 4, length - 4, 0))
        return TIKU_IPC_INVALID;
    return tiku_ble_packet_push(&incoming, p + 4, length - 4);
}
const tiku_ipc_wire_sink_t tiku_ble_service_sink = {consume, NULL, NULL};
/** @brief Send a monotonic request; retries preserve its identifier and payload. */
static void begin(uint8_t operation)
{
    if (serial == UINT32_MAX) {
        offline(1);
        return;
    }
    op = operation;
    request_length = op == TIKU_RADIO_POWER ? 17 : 16;
    tiku_radio_header(request, op, ++serial, claim, 0, request_length - 16);
    request[16] = 1;
    deadline = tiku_clock_time() + 5 * TIKU_CLOCK_SECOND;
    retry = tiku_clock_time();
}
/** @brief Apply only a response for the current operation and request identifier. */
static void response(const uint8_t *p, uint16_t n)
{
    if (!tiku_radio_valid(p, n, 1) || !op || p[3] != (op | 128u) ||
        tiku_radio_get32(p + 4) != serial)
        return;
    uint8_t done = op;
    op = 0;
    if (p[12] != TIKU_RADIO_OK) {
        offline(1);
        return;
    }
    if (done == TIKU_RADIO_DISCOVER) {
        if (n != 28 || tiku_radio_get32(p + 16) != TIKU_BLE_CAPABILITIES ||
            tiku_radio_get32(p + 24) != TIKU_BLE_PACKET_MAX) {
            offline(1);
            return;
        }
        peer_build = tiku_radio_get32(p + 20);
        available = 1;
    } else if (done == TIKU_RADIO_CLAIM) {
        claim = tiku_radio_get32(p + 8);
        if (!claim) {
            offline(1);
            return;
        }
        renew = tiku_clock_time() + TIKU_RADIO_RENEW_SECONDS * TIKU_CLOCK_SECOND;
    } else if (done == TIKU_RADIO_RELEASE) {
        offline(0);
        result = TIKU_DRV_OK;
    } else if (tiku_radio_get32(p + 8) != claim) {
        offline(1);
    } else if (done == TIKU_RADIO_POWER) {
        powered = 1;
        desired = 0;
        result = TIKU_DRV_OK;
    } else if (done == TIKU_RADIO_RENEW) {
        renew = tiku_clock_time() + TIKU_RADIO_RENEW_SECONDS * TIKU_CLOCK_SECOND;
    }
}
TIKU_PROCESS_THREAD(ble_client, ev, data)
{
    (void)ev;
    (void)data;
    TIKU_PROCESS_BEGIN();
    if (tiku_ipc_open("c6/ble", &endpoint))
        TIKU_PROCESS_EXIT();
    for (;;) {
        if (session != wire->local_session || ((claim || available) && !wire->ready)) {
            offline(claim != 0);
            session = wire->local_session;
        }
        {
            tiku_ipc_received_t m;
            if (tiku_ipc_receive(endpoint, &m) == TIKU_IPC_OK) {
                if (!m.is_buffer)
                    response(m.data, m.length);
                else
                    (void)tiku_ipc_ack(endpoint, m.submission, TIKU_IPC_UNSUPPORTED);
            }
        }
        if (wire->ready && !op) {
            if (!available)
                begin(TIKU_RADIO_DISCOVER);
            else if (desired == 2 && claim)
                begin(TIKU_RADIO_RELEASE);
            else if (desired == 2) {
                desired = 0;
                result = TIKU_DRV_OK;
            } else if (desired == 1 && !claim && !sender.active)
                begin(TIKU_RADIO_CLAIM);
            else if (desired == 1 && claim && !powered)
                begin(TIKU_RADIO_POWER);
            else if (claim && !TIKU_CLOCK_LT(tiku_clock_time(), renew))
                begin(TIKU_RADIO_RENEW);
        }
        if (op) {
            if (!TIKU_CLOCK_LT(tiku_clock_time(), deadline))
                offline(1);
            else if (!TIKU_CLOCK_LT(tiku_clock_time(), retry) &&
                     tiku_ipc_send(endpoint, request, request_length) == TIKU_IPC_OK)
                retry = tiku_clock_time() + TIKU_CLOCK_SECOND / 2;
        }
        if (tiku_ble_sender_step(&sender, endpoint, powered ? claim : 0) < 0)
            offline(1);
        tiku_timer_set_event(&timer, 1);
        TIKU_PROCESS_WAIT_EVENT();
    }
    TIKU_PROCESS_END();
}
void tiku_ble_service_start(tiku_ipc_wire_t *w)
{
    wire = w;
    if (!ble_client.is_running)
        tiku_process_start(&ble_client, NULL);
}
/** @brief Progress mailbox work while excluding the calling process from dispatch. */
static void wait_packet(uint16_t ms)
{
    const struct tiku_process *caller = TIKU_THIS();
    tiku_clock_time_t until = tiku_clock_time() + ((uint32_t)ms * TIKU_CLOCK_SECOND + 999u) / 1000u;
    do {
        tiku_watchdog_kick();
        (void)tiku_sched_run_once_except(caller);
    } while (!incoming.count && powered && TIKU_CLOCK_LT(tiku_clock_time(), until));
}
/** @brief Admit a complete host packet, with bounded backpressure when both slots are full. */
static int send_packet(const uint8_t *p, uint16_t n)
{
    uint8_t packet[TIKU_BLE_PACKET_MAX];
    if (!powered)
        return TIKU_DRV_ERR_NOT_PRESENT;
    if (!tiku_ble_packet_valid(p, n, 1))
        return TIKU_DRV_ERR_INVALID;
    memcpy(packet, p, n);
    const struct tiku_process *caller = TIKU_THIS();
    tiku_clock_time_t until = tiku_clock_time() + TIKU_CLOCK_SECOND;
    while (powered && sender.packets.count == TIKU_BLE_QUEUE &&
           TIKU_CLOCK_LT(tiku_clock_time(), until)) {
        tiku_watchdog_kick();
        (void)tiku_sched_run_once_except(caller);
    }
    if (!powered)
        return TIKU_DRV_ERR_NOT_PRESENT;
    if (tiku_ble_packet_push(&sender.packets, packet, n))
        return TIKU_DRV_ERR_TIMEOUT;
    tiku_process_poll(&ble_client);
    return TIKU_DRV_OK;
}
/** @brief Return a queued HCI packet without dispatching another process. */
static int receive_packet(uint8_t *p, uint16_t capacity)
{
    if (!powered)
        return TIKU_DRV_ERR_NOT_PRESENT;
    if (!incoming.count)
        return 0;
    unsigned n = incoming.length[incoming.head];
    if (!p || n > capacity)
        return TIKU_DRV_ERR_INVALID;
    memcpy(p, incoming.data[incoming.head], n);
    tiku_ble_packet_pop(&incoming);
    return (int)n;
}
static int ready(void)
{
    return powered && wire && wire->ready && session == wire->local_session;
}
static const char *version(void)
{
    return "C6 mailbox HCI v1";
}
static const tiku_bt_transport_t transport = {send_packet, receive_packet, ready, wait_packet,
                                              version};
int tiku_bt_controller_power(uint8_t on)
{
    if (!wire || !wire->ready)
        return TIKU_DRV_ERR_NOT_PRESENT;
    if (busy || TIKU_THIS() == &ble_client)
        return TIKU_DRV_ERR_TIMEOUT;
    if (on && ready())
        return TIKU_DRV_OK;
    if (!on)
        tiku_bt_shutdown();
    busy = 1;
    desired = on ? 1 : 2;
    result = TIKU_DRV_ERR_TIMEOUT;
    tiku_process_poll(&ble_client);
    const struct tiku_process *caller = TIKU_THIS();
    tiku_clock_time_t until = tiku_clock_time() + 20 * TIKU_CLOCK_SECOND;
    while (desired && TIKU_CLOCK_LT(tiku_clock_time(), until)) {
        tiku_watchdog_kick();
        (void)tiku_sched_run_once_except(caller);
    }
    int rc = desired ? TIKU_DRV_ERR_TIMEOUT : result;
    if (!rc && on) {
        (void)tiku_bt_register_transport(&transport);
        rc = tiku_bt_init();
        if (rc) {
            powered = 0;
            tiku_bt_shutdown();
            desired = 2;
            tiku_process_poll(&ble_client);
        }
    }
    busy = 0;
    return rc;
}
/** @brief Publish negotiated controller capabilities and packet counters. */
static int peer_read(char *out, size_t capacity)
{
    return snprintf(out, capacity,
                    "available=%u protocol=1 build=%lu claim=%lu session=%lu powered=%u\n"
                    "hci=260 queues=2 advertising=1 scanning=1 connections=1\n"
                    "tx=%lu rx=%lu faults=%lu\n",
                    available, (unsigned long)peer_build, (unsigned long)claim,
                    (unsigned long)session, powered, (unsigned long)sender.packets.delivered,
                    (unsigned long)incoming.delivered, (unsigned long)faults);
}
static const tiku_vfs_node_t nodes[] = {{.name = "peer", .type = TIKU_VFS_FILE, .read = peer_read}};
/** @brief Require the boot-owned mailbox before publishing its descriptor. */
static int init(void)
{
    return wire ? TIKU_DRV_OK : TIKU_DRV_ERR_NOT_PRESENT;
}
const tiku_drv_t tiku_drv_ble_ipc = {.name = "ble-ipc",
                                     .class = TIKU_DRV_CLASS_BLE,
                                     .init = init,
                                     .vfs_mount = "ble-ipc0",
                                     .vfs_nodes = nodes,
                                     .vfs_node_count = 1};
