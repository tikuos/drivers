/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_wifi_ipc.c - P4 wireless facade over the C6 IPC service.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <services/ipc/tiku_ipc_radio.h>
#include <interfaces/wireless/tiku_wireless.h>
#include <kernel/scheduler/tiku_sched.h>
#include <hal/tiku_watchdog_hal.h>
#include <string.h>
#include <stdio.h>
#if TIKU_P4_RADIO_TEST
#include <TikuBench/tests/esp32p4/test_radio.h>
#endif
#include <kernel/timers/tiku_timer.h>
TIKU_PROCESS(radio_client, "P4 Wi-Fi IPC");
static tiku_ipc_wire_t *control_wire, *data_wire;
static tiku_ipc_endpoint_t control_endpoint, data_endpoint;
static struct tiku_timer timer;
static tiku_radio_sender_t sender __attribute__((section(".p4.ipc"), aligned(64)));
static tiku_radio_frames_t receiver __attribute__((section(".p4.ipc"), aligned(64)));
static tiku_wireless_status_t cached;
static tiku_wireless_ap_t aps[TIKU_WIRELESS_MAX_SCAN_RESULTS]
    __attribute__((section(".p4.ipc"), aligned(64)));
static tiku_wireless_rx_t rx;
static void *rx_context;
static uint32_t session, data_session, claim, serial, scan_generation, peer_build;
static uint8_t discovered, want_power, profile, request[64], request_length, active_op;
static uint8_t desired_op, desired_arg, active_page, ap_next, ap_total, ssid_pending;
static int command_result;
static uint8_t secret[TIKU_RADIO_CREDENTIALS], secret_length, secret_pending, secret_sent;
static tiku_ipc_buffer_t secret_buffer;
static tiku_ipc_submission_t secret_submission;
static char saved_ssid[33], saved_psk[64];
static tiku_clock_time_t deadline, retry, renew_at, status_at;
static uint8_t pumping;
/** @brief Publish link events after updating the status cache. */
static void link_state(uint8_t state)
{
    uint8_t old = cached.link_state;
    cached.link_state = state;
    if (old != state && (old == TIKU_WIRELESS_LINK_JOINED || state == TIKU_WIRELESS_LINK_JOINED))
        (void)tiku_process_post(TIKU_PROCESS_BROADCAST,
                                state == TIKU_WIRELESS_LINK_JOINED ? TIKU_WIRELESS_EVT_LINK_UP
                                                                   : TIKU_WIRELESS_EVT_LINK_DOWN,
                                NULL);
}
/** @brief Drop packet admission before cancelling loans on a lost peer session. */
static void offline(void)
{
    discovered = 0;
    claim = 0;
    cached.up = 0;
    link_state(TIKU_WIRELESS_LINK_IDLE);
    active_op = 0;
    request_length = 0;
    ap_next = ap_total = 0;
    cached.scan_in_progress = 0;
    tiku_radio_sender_cancel(&sender, data_endpoint);
    memset(&receiver, 0, sizeof receiver);
    if (secret_pending)
        (void)tiku_ipc_cancel(control_endpoint, secret_submission);
    if (desired_op) {
        desired_op = 0;
        command_result = TIKU_DRV_ERR_TIMEOUT;
    }
    secret_sent = 0;
    secret_length = 0;
    tiku_radio_wipe(secret, sizeof secret);
}
/** @brief Receive complete frames only while the current claim has a joined link. */
static tiku_ipc_err_t frame_input(void *unused, const void *data, uint16_t n)
{
    (void)unused;
    if (!claim || !control_wire || !control_wire->ready || control_wire->local_session != session ||
        cached.link_state != TIKU_WIRELESS_LINK_JOINED)
        return TIKU_IPC_DOWN;
    return tiku_radio_frame_consume(&receiver, data, n);
}
const tiku_ipc_wire_sink_t tiku_radio_control_sink = {NULL, NULL, NULL};
const tiku_ipc_wire_sink_t tiku_radio_data_sink = {frame_input, NULL, NULL};
int tiku_radio_local_allowed(void)
{
    return 1;
}
/** @brief Build one idempotent request with a monotonically increasing identifier. */
static void begin(uint8_t op, const void *payload, uint8_t length)
{
    if (serial == UINT32_MAX)
        return;
    active_op = op;
    request_length = 16 + length;
    tiku_radio_header(request, op, ++serial, op == TIKU_RADIO_CLAIM ? 0 : claim, 0, length);
    if (length)
        memcpy(request + 16, payload, length);
    deadline = tiku_clock_time() + 20 * TIKU_CLOCK_SECOND;
    retry = tiku_clock_time();
    if (op == TIKU_RADIO_CONNECT) {
        unsigned sl = strlen(saved_ssid), pl = strlen(saved_psk);
        secret_length = 19 + sl + pl;
        tiku_radio_header(secret, TIKU_RADIO_CREDENTIAL, serial, claim, 0, 3 + sl + pl);
        secret[16] = 0;
        secret[17] = sl;
        secret[18] = pl;
        memcpy(secret + 19, saved_ssid, sl);
        memcpy(secret + 19 + sl, saved_psk, pl);
        secret_sent = 0;
    }
}
/** @brief Keep ownership until the credential loan completes, then erase its backing. */
static void credential_step(void)
{
    if (secret_pending) {
        tiku_ipc_completion_t c;
        if (tiku_ipc_completion(control_endpoint, &c) == TIKU_IPC_OK) {
            void *p;
            uint16_t n;
            if (tiku_ipc_buffer_map(secret_buffer, &p, &n) == TIKU_IPC_OK)
                tiku_radio_wipe(p, n);
            (void)tiku_ipc_buffer_release(secret_buffer);
            secret_buffer = (tiku_ipc_buffer_t){0};
            secret_pending = 0;
            if (active_op == TIKU_RADIO_CONNECT && c.status == TIKU_IPC_OK)
                secret_sent = 1;
            else if (active_op == TIKU_RADIO_CONNECT) {
                active_op = 0;
                desired_op = 0;
                command_result = TIKU_DRV_ERR_IO;
            }
            tiku_radio_wipe(secret, sizeof secret);
        }
    }
    if (active_op != TIKU_RADIO_CONNECT || secret_sent || secret_pending)
        return;
    if (!secret_length)
        return;
    if (tiku_ipc_buffer_alloc(secret_length, &secret_buffer) != TIKU_IPC_OK)
        return;
    void *p;
    uint16_t n;
    if (tiku_ipc_buffer_map(secret_buffer, &p, &n) != TIKU_IPC_OK) {
        (void)tiku_ipc_buffer_release(secret_buffer);
        return;
    }
    memcpy(p, secret, secret_length);
    if (tiku_ipc_submit(control_endpoint, secret_buffer, TIKU_IPC_INPUT, 3 * TIKU_CLOCK_SECOND,
                        &secret_submission) == TIKU_IPC_OK)
        secret_pending = 1;
    else {
        tiku_radio_wipe(p, n);
        (void)tiku_ipc_buffer_release(secret_buffer);
    }
}
/** @brief Validate and apply one response to its matching outstanding request. */
static void response(const uint8_t *p, uint16_t n)
{
    if (!active_op || !tiku_radio_valid(p, n, 1) || p[3] != (active_op | 128u) ||
        tiku_radio_get32(p + 4) != serial)
        return;
    uint8_t op = active_op, r = p[12], length = p[13];
    const uint8_t *v = p + 16;
    uint32_t remote_claim = tiku_radio_get32(p + 8);
    if (op != TIKU_RADIO_CLAIM && op != TIKU_RADIO_RELEASE && remote_claim != claim) {
        offline();
        return;
    }
    active_op = 0;
    if (r) {
        if (desired_op == op) {
            desired_op = 0;
            command_result = r == TIKU_RADIO_BUSY ? TIKU_DRV_ERR_TIMEOUT : TIKU_DRV_ERR_INVALID;
        }
        if (r == TIKU_RADIO_STALE)
            offline();
        return;
    }
    switch (op) {
    case TIKU_RADIO_DISCOVER:
        if (length != 12 || tiku_radio_get32(v) != TIKU_RADIO_CAPABILITIES ||
            ((unsigned)v[8] | (unsigned)v[9] << 8) != TIKU_RADIO_FRAME_MAX || v[10] != 2) {
            offline();
            return;
        }
        discovered = 1;
        peer_build = tiku_radio_get32(v + 4);
        break;
    case TIKU_RADIO_CLAIM:
        if (length || !remote_claim) {
            offline();
            return;
        }
        claim = remote_claim;
        renew_at = tiku_clock_time() + TIKU_RADIO_RENEW_SECONDS * TIKU_CLOCK_SECOND;
        break;
    case TIKU_RADIO_RENEW:
        renew_at = tiku_clock_time() + TIKU_RADIO_RENEW_SECONDS * TIKU_CLOCK_SECOND;
        break;
    case TIKU_RADIO_RELEASE:
        claim = 0;
        break;
    case TIKU_RADIO_POWER:
        cached.up = request[16];
        if (!cached.up)
            link_state(TIKU_WIRELESS_LINK_IDLE);
        status_at = 0;
        break;
    case TIKU_RADIO_STATUS:
        if (!active_page) {
            if (length != 44 || v[3] > TIKU_WIRELESS_LINK_FAILED ||
                v[4] > TIKU_WIRELESS_MAX_SCAN_RESULTS || v[5] > 32) {
                offline();
                return;
            }
            uint8_t was_scanning = cached.scan_in_progress;
            cached.up = v[0];
            cached.scan_in_progress = v[1] || desired_op == TIKU_RADIO_SCAN;
            cached.disconnect_pending = v[2];
            cached.scan_aps_found = v[4];
            cached.joined_ssid_len = v[5];
            memcpy(cached.mac, v + 6, 6);
            memcpy(cached.joined_bssid, v + 12, 6);
            cached.rssi_dbm = (int16_t)((uint16_t)v[18] | (uint16_t)v[19] << 8);
            cached.last_scan_ticks = tiku_radio_get32(v + 20);
            cached.last_join_ticks = tiku_radio_get32(v + 24);
            cached.link_status_raw = tiku_radio_get32(v + 28);
            uint32_t gen = tiku_radio_get32(v + 32);
            if (gen != scan_generation) {
                scan_generation = gen;
                ap_next = 0;
                memset(aps, 0, sizeof aps);
            }
            if (!cached.scan_in_progress) {
                ap_total = v[4];
                if (was_scanning && !ap_total)
                    (void)tiku_process_post(TIKU_PROCESS_BROADCAST,
                                            TIKU_WIRELESS_EVT_SCAN_COMPLETE, NULL);
            }
            link_state(v[3]);
            ssid_pending = 1;
        } else {
            if (length != 32) {
                offline();
                return;
            }
            memcpy(cached.joined_ssid, v, 32);
            ssid_pending = 0;
        }
        status_at = tiku_clock_time() + TIKU_CLOCK_SECOND / 4;
        break;
    case TIKU_RADIO_AP:
        if (length != 42 || v[6] > 32 || ap_next >= TIKU_WIRELESS_MAX_SCAN_RESULTS) {
            offline();
            return;
        }
        {
            tiku_wireless_ap_t *a = &aps[ap_next++];
            memcpy(a->bssid, v, 6);
            a->ssid_len = v[6];
            memcpy(a->ssid, v + 7, 32);
            a->rssi = (int16_t)((uint16_t)v[39] | (uint16_t)v[40] << 8);
            a->channel = v[41];
            (void)tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_WIRELESS_EVT_AP_FOUND, a);
            if (ap_next == ap_total)
                (void)tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_WIRELESS_EVT_SCAN_COMPLETE,
                                        (void *)(uintptr_t)ap_total);
        }
        break;
    case TIKU_RADIO_CONNECT:
        link_state(TIKU_WIRELESS_LINK_CONNECTING);
        break;
    case TIKU_RADIO_DISCONNECT:
    case TIKU_RADIO_FORGET:
        link_state(TIKU_WIRELESS_LINK_IDLE);
        break;
    default:
        break;
    }
    if (desired_op == op) {
        desired_op = 0;
        command_result = TIKU_DRV_OK;
    }
}
/** @brief Choose control work before sending Ethernet chunks. */
static void control_step(void)
{
    if (!control_wire->ready || secret_pending)
        return;
    if (!active_op) {
        if (!discovered)
            begin(TIKU_RADIO_DISCOVER, NULL, 0);
        else if (!claim && (want_power || desired_op))
            begin(TIKU_RADIO_CLAIM, NULL, 0);
        else if (claim && !TIKU_CLOCK_LT(tiku_clock_time(), renew_at))
            begin(TIKU_RADIO_RENEW, NULL, 0);
        else if (desired_op)
            begin(desired_op, desired_op == TIKU_RADIO_POWER ? &desired_arg : NULL,
                  desired_op == TIKU_RADIO_POWER ? 1 : 0);
        else if (want_power && !cached.up) {
            uint8_t on = 1;
            begin(TIKU_RADIO_POWER, &on, 1);
        } else if (want_power && profile && cached.link_state == TIKU_WIRELESS_LINK_IDLE)
            begin(TIKU_RADIO_CONNECT, NULL, 0);
        else if (!want_power && claim)
            begin(TIKU_RADIO_RELEASE, NULL, 0);
        else if (claim && ap_next < ap_total) {
            uint8_t p[5];
            tiku_radio_put32(p, scan_generation);
            p[4] = ap_next;
            begin(TIKU_RADIO_AP, p, 5);
        } else if (!TIKU_CLOCK_LT(tiku_clock_time(), status_at)) {
            active_page = ssid_pending ? 1 : 0;
            begin(TIKU_RADIO_STATUS, &active_page, 1);
        }
    }
    credential_step();
    if (active_op && (!secret_pending) && (active_op != TIKU_RADIO_CONNECT || secret_sent)) {
        if (!TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
            offline();
            return;
        }
        if (!TIKU_CLOCK_LT(tiku_clock_time(), retry) &&
            tiku_ipc_send(control_endpoint, request, request_length) == TIKU_IPC_OK)
            retry = tiku_clock_time() + TIKU_CLOCK_SECOND / 2;
    }
}
TIKU_PROCESS_THREAD(radio_client, ev, data)
{
    (void)ev;
    (void)data;
    TIKU_PROCESS_BEGIN();
    if (tiku_ipc_open("c6/radio", &control_endpoint) || tiku_ipc_open("c6/wifi", &data_endpoint))
        TIKU_PROCESS_EXIT();
    for (;;) {
        if (session != control_wire->local_session) {
            offline();
            session = control_wire->local_session;
        }
        if (claim && !control_wire->ready)
            offline();
        if (data_wire && data_session != data_wire->local_session) {
            data_session = data_wire->local_session;
            tiku_radio_sender_cancel(&sender, data_endpoint);
            memset(&receiver, 0, sizeof receiver);
            if (cached.link_state == TIKU_WIRELESS_LINK_JOINED)
                link_state(TIKU_WIRELESS_LINK_IDLE);
        }
        credential_step();
        {
            tiku_ipc_received_t m;
            if (tiku_ipc_receive(control_endpoint, &m) == TIKU_IPC_OK && !m.is_buffer)
                response(m.data, m.length);
        }
        control_step();
        if (!claim && !sender.active)
            sender.frames = (tiku_radio_frames_t){0};
        tiku_radio_sender_step(&sender, data_endpoint);
        {
            uint16_t n;
            const uint8_t *p = tiku_radio_frame_peek(&receiver, &n);
            if (p) {
                if (rx)
                    rx(p, n, rx_context);
                tiku_radio_frame_pop(&receiver);
            }
        }
        tiku_radio_frame_expire(&receiver, tiku_clock_time());
        tiku_timer_set_event(&timer, 1);
        TIKU_PROCESS_WAIT_EVENT();
    }
    TIKU_PROCESS_END();
}
void tiku_radio_service_start(tiku_ipc_wire_t *control, tiku_ipc_wire_t *data)
{
    control_wire = control;
    data_wire = data;
    (void)data_wire;
    if (!radio_client.is_running)
        tiku_process_start(&radio_client, NULL);
}
/** @brief Queue one facade operation without borrowing another process's IPC binding. */
static int enqueue(uint8_t op, uint8_t arg)
{
    if (!control_wire)
        return TIKU_DRV_ERR_NOT_PRESENT;
    if (desired_op)
        return TIKU_DRV_ERR_TIMEOUT;
    desired_op = op;
    desired_arg = arg;
    command_result = TIKU_DRV_ERR_TIMEOUT;
    tiku_process_poll(&radio_client);
    return TIKU_DRV_OK;
}
int tiku_wireless_status(tiku_wireless_status_t *out)
{
    if (!out)
        return TIKU_DRV_ERR_INVALID;
    *out = cached;
    return TIKU_DRV_OK;
}
int tiku_wireless_power(uint8_t on)
{
    if (!control_wire || !control_wire->ready) {
        if (!on) {
            want_power = profile = 0;
            tiku_radio_wipe(saved_psk, sizeof saved_psk);
            tiku_radio_wipe(saved_ssid, sizeof saved_ssid);
            offline();
        }
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    if (pumping || TIKU_THIS() == &radio_client)
        return TIKU_DRV_ERR_TIMEOUT;
    if (!on) {
        profile = 0;
        tiku_radio_wipe(saved_psk, sizeof saved_psk);
        tiku_radio_wipe(saved_ssid, sizeof saved_ssid);
    }
    int r = enqueue(TIKU_RADIO_POWER, !!on);
    if (r)
        return r;
    want_power = !!on;
    const struct tiku_process *caller = TIKU_THIS();
    tiku_clock_time_t until = tiku_clock_time() + 25 * TIKU_CLOCK_SECOND;
    pumping = 1;
    while (desired_op && TIKU_CLOCK_LT(tiku_clock_time(), until)) {
        tiku_watchdog_kick();
        (void)tiku_sched_run_once_except(caller);
    }
    pumping = 0;
    return desired_op ? TIKU_DRV_ERR_TIMEOUT : command_result;
}
int tiku_wireless_scan_start(void)
{
    if (!cached.up)
        return TIKU_DRV_ERR_INVALID;
    if (cached.scan_in_progress || cached.link_state == TIKU_WIRELESS_LINK_CONNECTING)
        return TIKU_DRV_ERR_TIMEOUT;
    int r = enqueue(TIKU_RADIO_SCAN, 0);
    if (!r) {
        cached.scan_in_progress = 1;
        ap_next = ap_total = 0;
    }
    return r;
}
uint8_t tiku_wireless_scan_results(tiku_wireless_ap_t *out, uint8_t maximum)
{
    uint8_t n = ap_next < maximum ? ap_next : maximum;
    if (!out)
        return 0;
    memcpy(out, aps, n * sizeof *out);
    return n;
}
int tiku_wireless_connect_auth(const char *ssid, const char *psk, tiku_wireless_auth_t auth)
{
    if (!ssid || !psk || !cached.up || auth != TIKU_WIRELESS_AUTH_WPA2_PSK)
        return TIKU_DRV_ERR_INVALID;
    size_t sl = strnlen(ssid, 33), pl = strnlen(psk, 64);
    if (!sl || sl > 32 || (pl && (pl < 8 || pl > 63)))
        return TIKU_DRV_ERR_INVALID;
    if (cached.scan_in_progress || cached.link_state == TIKU_WIRELESS_LINK_CONNECTING)
        return TIKU_DRV_ERR_TIMEOUT;
    int r = enqueue(TIKU_RADIO_CONNECT, 0);
    if (r)
        return r;
    memcpy(saved_ssid, ssid, sl + 1);
    memcpy(saved_psk, psk, pl + 1);
    profile = 1;
    return TIKU_DRV_OK;
}
int tiku_wireless_connect(const char *ssid, const char *psk)
{
    return tiku_wireless_connect_auth(ssid, psk, TIKU_WIRELESS_AUTH_WPA2_PSK);
}
int tiku_wireless_disconnect(void)
{
    if (!cached.up)
        return TIKU_DRV_ERR_INVALID;
    int r = enqueue(TIKU_RADIO_DISCONNECT, 0);
    if (!r)
        profile = 0;
    return r;
}
int tiku_wireless_forget(void)
{
    if (desired_op)
        return TIKU_DRV_ERR_TIMEOUT;
    profile = 0;
    tiku_radio_wipe(saved_psk, sizeof saved_psk);
    tiku_radio_wipe(saved_ssid, sizeof saved_ssid);
    return cached.up ? enqueue(TIKU_RADIO_FORGET, 0) : TIKU_DRV_OK;
}
int tiku_wireless_saved_profile(tiku_wireless_saved_profile_t *out)
{
    if (!out)
        return TIKU_DRV_ERR_INVALID;
    memset(out, 0, sizeof *out);
    return TIKU_DRV_ERR_NOT_PRESENT;
}
int tiku_wireless_tx_eth(const uint8_t *p, uint16_t n)
{
    if (!claim || !control_wire->ready || control_wire->local_session != session ||
        cached.link_state != TIKU_WIRELESS_LINK_JOINED)
        return TIKU_DRV_ERR_INVALID;
    tiku_ipc_err_t r = tiku_radio_frame_push(&sender.frames, p, n);
    return r == TIKU_IPC_OK      ? TIKU_DRV_OK
           : r == TIKU_IPC_AGAIN ? TIKU_DRV_ERR_TIMEOUT
                                 : TIKU_DRV_ERR_INVALID;
}
int tiku_wireless_set_rx(tiku_wireless_rx_t cb, void *context)
{
    rx = cb;
    rx_context = context;
    return TIKU_DRV_OK;
}
int tiku_wireless_rx_poll(void)
{
    if (pumping || TIKU_THIS() == &radio_client)
        return 0;
    uint32_t before = receiver.delivered;
    pumping = 1;
    (void)tiku_sched_run_once_except(TIKU_THIS());
    pumping = 0;
    return (int)(receiver.delivered - before);
}
/** @brief Confirm boot supplied the two remote service endpoints. */
static int init(void)
{
    return control_wire ? TIKU_DRV_OK : TIKU_DRV_ERR_NOT_PRESENT;
}
/** @brief Render discovered peer identity and bounded queue counters. */
static int peer_read(char *out, size_t capacity)
{
    return snprintf(out, capacity,
                    "available=%u protocol=1 build=%lu claim=%lu session=%lu\n"
                    "station=open,wpa2 max_frame=1514 queues=2\n"
                    "tx=%lu tx_drop=%lu rx=%lu rx_drop=%lu rx_bad=%lu\n",
                    (unsigned)discovered, (unsigned long)peer_build, (unsigned long)claim,
                    (unsigned long)session, (unsigned long)sender.frames.delivered,
                    (unsigned long)sender.frames.drops, (unsigned long)receiver.delivered,
                    (unsigned long)receiver.drops, (unsigned long)receiver.bad);
}
static const tiku_vfs_node_t nodes[] = {
    {.name = "peer", .type = TIKU_VFS_FILE, .read = peer_read},
#if TIKU_P4_RADIO_TEST
    {.name = "test",
     .type = TIKU_VFS_FILE,
     .read = tiku_p4_radio_test_read,
     .write = tiku_p4_radio_test_write,
     .req_cap = TIKU_VFS_CAP_SYS},
#endif
};
const tiku_drv_t tiku_drv_wifi_ipc = {.name = "wifi-ipc",
                                      .class = TIKU_DRV_CLASS_WIFI,
                                      .init = init,
                                      .vfs_mount = "ipc0",
                                      .vfs_nodes = nodes,
                                      .vfs_node_count = sizeof nodes / sizeof nodes[0]};
