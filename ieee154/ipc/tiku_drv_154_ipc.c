/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_drv_154_ipc.c - P4 IEEE 802.15.4 HAL through the C6 mailbox owner.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_ieee154_hal.h>
#include <hal/tiku_watchdog_hal.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/timers/tiku_timer.h>
#include <services/ipc/tiku_ipc_154.h>
#include <tikukits/crypto/ccm/tiku_kits_crypto_ccm.h>
#include <string.h>
TIKU_PROCESS(radio154_client, "P4 802.15.4 IPC");
static tiku_ipc_wire_t *wire;
static tiku_ipc_endpoint_t endpoint;
static struct tiku_timer timer;
static uint32_t session, serial, claim;
static tiku_clock_time_t deadline, retry, renew;
static uint8_t request[64], response[64], request_n, response_n, op, active, busy;
static uint8_t ack[3], ack_n;
static int result;
/** @brief Invalidate the local lease and any ACK from an earlier transaction. */
static void offline(void)
{
    claim = active = op = ack_n = 0;
    result = -1;
}
/** @brief Queue one monotonic request; retransmission preserves its bytes. */
static int begin(uint8_t operation, const uint8_t *data, unsigned n)
{
    if (!wire || !wire->ready || session != wire->local_session || op || serial == UINT32_MAX || n > 48)
        return -1;
    op = operation;
    request_n = 16 + n;
    tiku_154_ipc_header(request, op, ++serial, claim, 0, n);
    if (n) memcpy(request + 16, data, n);
    deadline = tiku_clock_time() + 3 * TIKU_CLOCK_SECOND;
    retry = tiku_clock_time();
    result = -1;
    tiku_process_poll(&radio154_client);
    return 0;
}
/** @brief Accept a response for the current request and lease generation. */
static void receive_response(const uint8_t *p, unsigned n)
{
    if (!op || !tiku_154_ipc_valid(p, n, 1) ||
        p[3] != (op | 128u) || tiku_radio_get32(p + 4) != serial) return;
    if (p[12] || (op != TIKU_RADIO_DISCOVER && op != TIKU_RADIO_CLAIM &&
        op != TIKU_RADIO_RELEASE && tiku_radio_get32(p + 8) != claim)) {
        offline();
        return;
    }
    if (op == TIKU_RADIO_CLAIM) claim = tiku_radio_get32(p + 8);
    if (op == TIKU_RADIO_RELEASE) claim = active = ack_n = 0;
    memcpy(response, p, n);
    response_n = n;
    result = 0;
    op = 0;
    renew = tiku_clock_time() + TIKU_RADIO_RENEW_SECONDS * TIKU_CLOCK_SECOND;
}
TIKU_PROCESS_THREAD(radio154_client, ev, data)
{
    (void)ev;
    (void)data;
    TIKU_PROCESS_BEGIN();
    if (tiku_ipc_open("c6/154", &endpoint)) TIKU_PROCESS_EXIT();
    for (;;) {
        if (!wire->ready || session != wire->local_session) {
            offline();
            session = wire->local_session;
        }
        {
            tiku_ipc_received_t m;
            if (tiku_ipc_receive(endpoint, &m) == TIKU_IPC_OK) {
                if (m.is_buffer) (void)tiku_ipc_ack(endpoint, m.submission, TIKU_IPC_UNSUPPORTED);
                else receive_response(m.data, m.length);
            }
        }
        if (claim && !busy && !op && !TIKU_CLOCK_LT(tiku_clock_time(), renew))
            (void)begin(TIKU_RADIO_RENEW, NULL, 0);
        if (op) {
            if (!TIKU_CLOCK_LT(tiku_clock_time(), deadline)) offline();
            else if (!TIKU_CLOCK_LT(tiku_clock_time(), retry) &&
                tiku_ipc_send(endpoint, request, request_n) == TIKU_IPC_OK)
                retry = tiku_clock_time() + TIKU_CLOCK_SECOND / 4;
        }
        tiku_timer_set_event(&timer, 1);
        TIKU_PROCESS_WAIT_EVENT();
    }
    TIKU_PROCESS_END();
}
void tiku_154_service_start(tiku_ipc_wire_t *w)
{
    wire = w;
    if (!radio154_client.is_running) tiku_process_start(&radio154_client, NULL);
}
/** @brief Wait for the owning process while excluding the HAL caller from dispatch. */
static int rpc(uint8_t operation, const uint8_t *data, unsigned n)
{
    const struct tiku_process *caller = TIKU_THIS();
    tiku_clock_time_t until = tiku_clock_time() + 4 * TIKU_CLOCK_SECOND;
    while (op && TIKU_CLOCK_LT(tiku_clock_time(), until)) {
        tiku_watchdog_kick();
        (void)tiku_sched_run_once_except(caller);
    }
    if (begin(operation, data, n)) return -1;
    while (op && TIKU_CLOCK_LT(tiku_clock_time(), until)) {
        tiku_watchdog_kick();
        (void)tiku_sched_run_once_except(caller);
    }
    if (op) offline();
    return result;
}
int tiku_ieee154_arch_available(void) { return wire && wire->ready; }
int tiku_ieee154_arch_mode_154(uint8_t channel)
{
    if (busy) return -1;
    busy = 1;
    channel = channel < 11 ? 11 : channel > 26 ? 26 : channel;
    int rc = 0;
    if (!claim) {
        rc = rpc(TIKU_RADIO_DISCOVER, NULL, 0);
        if (!rc && (response_n != 22 || tiku_radio_get32(response + 16) != TIKU_154_IPC_CAPS ||
            response[20] != TIKU_154_MAX_PSDU || response[21] != TIKU_154_IPC_WINDOW_MS)) rc = -1;
        if (!rc) rc = rpc(TIKU_RADIO_CLAIM, NULL, 0);
        if (!rc && !claim) rc = -1;
    }
    if (!rc) rc = rpc(TIKU_154_MODE, &channel, 1);
    if (!rc) active = 1;
    else offline();
    ack_n = busy = 0;
    return rc;
}
void tiku_ieee154_arch_leave(void)
{
    if (busy) return;
    busy = 1;
    if (claim) (void)rpc(TIKU_RADIO_RELEASE, NULL, 0);
    offline();
    busy = 0;
}
void tiku_ieee154_arch_set_channel(uint8_t channel)
{
    if (active) (void)tiku_ieee154_arch_mode_154(channel);
}
int tiku_ieee154_arch_tx(const uint8_t *p, uint8_t n)
{
    if (!p || !n || n > TIKU_154_MAX_PSDU) return -1;
    if (busy || !active) return -2;
    busy = 1;
    ack_n = 0;
    int rc = 0;
    for (unsigned offset = 0; offset < n && !rc;) {
        uint8_t chunk[1 + TIKU_154_IPC_CHUNK];
        unsigned len = n - offset;
        if (len > TIKU_154_IPC_CHUNK) len = TIKU_154_IPC_CHUNK;
        chunk[0] = offset;
        memcpy(chunk + 1, p + offset, len);
        rc = rpc(TIKU_154_STAGE, chunk, len + 1);
        offset += len;
    }
    if (!rc) rc = rpc(TIKU_154_TX, &n, 1);
    if (!rc && response_n == 19 && (response[16] & 7) == 2 && n >= 3 && response[18] == p[2]) {
        memcpy(ack, response + 16, 3); ack_n = 3;
    }
    busy = 0;
    return rc ? -2 : 0;
}
/** @brief Receive through bounded C6 listen windows and copy the captured frame. */
static int receive(uint8_t *buf, uint8_t cap, uint32_t ms, int8_t *rssi,
                   int filtered, uint16_t pan, uint16_t addr, uint8_t *did_ack)
{
    if (!buf || !cap || !active || busy) return -1;
    busy = 1;
    if (!filtered && ack_n) {
        unsigned n = cap < ack_n ? cap : ack_n;
        memcpy(buf, ack, n); ack_n = busy = 0;
        if (rssi) *rssi = 0;
        return n;
    }
    ack_n = 0;
    if (ms > 60000) ms = 60000;
    tiku_clock_time_t until = tiku_clock_time() + (ms * TIKU_CLOCK_SECOND + 999) / 1000;
    int count = 0;
    do {
        uint8_t p[] = {ms < TIKU_154_IPC_WINDOW_MS ? ms : TIKU_154_IPC_WINDOW_MS,
            filtered, pan, pan >> 8, addr, addr >> 8};
        if (rpc(TIKU_154_RX, p, sizeof p) || response_n != 19 || response[16] > 125) {
            count = -1; break;
        }
        count = response[16];
        if (count) {
            if (rssi) *rssi = (int8_t)response[17];
            if (did_ack) *did_ack = response[18];
            if (count > cap) count = cap;
            for (unsigned offset = 0; offset < (unsigned)count;) {
                unsigned len = (unsigned)count - offset;
                if (len > TIKU_154_IPC_CHUNK) len = TIKU_154_IPC_CHUNK;
                uint8_t chunk[] = {offset, len};
                if (rpc(TIKU_154_READ, chunk, 2) || response_n != 16 + len) { count = -1; break; }
                memcpy(buf + offset, response + 16, len);
                offset += len;
            }
            break;
        }
    } while (TIKU_CLOCK_LT(tiku_clock_time(), until));
    busy = 0;
    return count;
}
int tiku_ieee154_arch_rx(uint8_t *buf, uint8_t cap, uint32_t ms, int8_t *rssi)
{ return receive(buf, cap, ms, rssi, 0, 0, 0, NULL); }
int tiku_ieee154_arch_rx_ack(uint8_t *buf, uint8_t cap, uint32_t ms, int8_t *rssi,
                           uint16_t pan, uint16_t addr, uint8_t *acknowledged)
{ return receive(buf, cap, ms, rssi, 1, pan, addr, acknowledged); }
int tiku_ieee154_arch_ed(uint8_t channel, int8_t *dbm)
{
    if (tiku_ieee154_arch_mode_154(channel)) return -1;
    busy = 1;
    channel = channel < 11 ? 11 : channel > 26 ? 26 : channel;
    int rc = rpc(TIKU_154_ED, &channel, 1);
    int level = !rc && response_n == 18 ? response[16] : -1;
    if (level >= 0 && dbm) *dbm = (int8_t)response[17];
    busy = 0;
    return level;
}
int tiku_ieee154_arch_cca(void)
{
    if (!active || busy) return 0;
    busy = 1;
    int rc = rpc(TIKU_154_CCA, NULL, 0);
    int idle = !rc && response_n == 17 && response[16] == 1;
    busy = 0;
    return idle;
}
void tiku_ieee154_arch_hold(int on) { (void)on; }
int tiku_ieee154_arch_ccm_star(int decrypt, const uint8_t key[16], const uint8_t nonce[13],
    const uint8_t *aad, size_t aad_n, const uint8_t *m, size_t m_n,
    uint8_t mic_n, uint8_t *out, uint8_t *mic)
{
    return tiku_kits_crypto_ccm_star(decrypt, key, nonce, aad, aad_n, m, m_n,
        mic_n, out, mic) == TIKU_KITS_CRYPTO_OK ? 0 : -1;
}
