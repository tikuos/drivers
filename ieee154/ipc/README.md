# FireBeetle P4 IEEE 802.15.4 over C6 IPC

The P4 uses `hal/tiku_ieee154_hal.h` and the MAC in `interfaces/radio`.
The C6 drives its native 2.4 GHz IEEE 802.15.4 MAC and PHY. Requests travel
through the kernel mailbox `c6/154`, on channel 5 of the SDIO multiplexer.
Channel 4 remains available to the SDR overlay.

Add `TIKU_C6_154_SERVICE_ENABLE=1` to the C6 Wi-Fi and BLE mailbox build in
`drivers/ble/ipc/README.md`. Add `TIKU_DRV_154_IPC_ENABLE=1` to its P4 build.
The service requires both mailbox profiles. The P4 build can also include
`EXTRA_CFLAGS=-DTIKU_SHELL_CMD_RADIO154=1` for the radio shell commands.

```text
bt off
wifi off
radio154 ed 15
radio154 tx 15 hello
radio154 rx 15 10
radio154 ping 15 10
radio154 pong 15 20
```

The HAL supports channels 11–26, frames of up to 125 bytes before the FCS,
energy detection, clear-channel assessment and receive-side automatic ACKs.
The native MAC appends and checks the FCS. The C6 sends requested receive
ACKs in hardware and captures transmit ACKs before returning a transaction
to the P4. AES-CCM* runs on the P4 through the existing crypto kit; keys do
not cross the mailbox.

## Ownership and transport

A claim belongs to one negotiated mailbox session and lasts ten seconds.
The P4 renews it every two seconds; accepted owner operations also extend
it. Loss of the session or expiry stops RF and discards staged frames.
The C6 refuses a claim while native radio work holds RF. While 802.15.4
owns RF, native Wi-Fi and BLE startup are refused. Stop those services
before entering 802.15.4 mode; simultaneous radio operation is excluded.

Discovery returns the protocol capabilities, frame limit and maximum listen
window. Frame staging and reads carry at most 40 bytes per inline mailbox
request. Transmit commits require a complete staged frame. Monotonic
request identifiers and cached responses prevent an identical retry from
sending the frame again. The service rejects changed requests with reused
identifiers, stale claims, malformed lengths and out-of-range offsets.

The C6 keeps reception armed between mailbox calls and queues 32 frames.
Receive and ACK completion use separate interrupts. A cleared interrupt leaves
an active reception intact; a missing ACK completion has a bounded timeout.
The queue takes 4096 bytes from the shared radio heap while 802.15.4 is on;
release returns those bytes before shutting down the shared radio core.
When the queue fills, the receiver stops until the P4 consumes a frame; no
new frame is acknowledged while admission is stopped. A receive transaction
waits up to 20 milliseconds for a queued frame. The P4 repeats transactions
for a requested listen interval, capped at 60 seconds per HAL call. Changing
channel, transmitting, measuring energy or releasing ownership ends queued
reception. RF transmit and ACK timing run on the C6; mailbox scheduling adds
latency to the P4 call. This service does not provide a Thread/Zigbee stack.

## Checks

```sh
make -C TikuBench/tests/host/kernel ipc-154 bt-multi c6-ble ipc-radio
```

The transaction suite checks ownership, duplicate suppression, session loss,
lease expiry, full-size frames and malformed chunks. Mutation checks remove
those guards and require the corresponding test to fail. Board captures
remain in `temp/` or `kintsugi/`.
