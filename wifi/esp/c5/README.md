# ESP32-C5 native radio adapters

This component controls the C5 radio's physical layer: initial calibration,
warm wake, and RF shutdown. An optional Wi-Fi build uses C5 vendor libraries
with the shared TikuOS OS adapter and C5 interrupt, clock and timer bindings.
There is no ESP-IDF or FreeRTOS runtime dependency. Separate opt-in profiles
provide BLE, both radios at once under Espressif's coexistence arbiter, and
receive-only SDR, and IEEE 802.15.4.

## Build

Fetch the C5 library and ROM bindings:

```sh
sh drivers/wifi/esp/c5/fetch.sh
make MCU=esp32c5 HAS_DRIVERS=1 TIKU_DRV_PHY_C5_ENABLE=1 \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESPTOOL=/path/to/venv/bin/esptool
```

The driver is optional and does not enable RF at boot. A PHY-only diagnostic
executes from internal SRAM; the Wi-Fi, BLE and SDR builds require XIP for ordinary vendor
code and constants. `fetch.sh` downloads the Apache-2.0 PHY
library pinned by ESP-IDF `4d59230ddff16327812782151ef0afef202dc6d7`:
`esp-phy-lib` commit `20f1db053a0e6cb9f1c09d255c43bf42483041d0`.
The download includes the license; fetching and building check SHA-256 hashes.
The C5 ROM bindings require ECO 2 or later, also checked by port startup.

For native Wi-Fi:

```sh
sh drivers/wifi/esp/c5/fetch.sh --wifi
make MCU=esp32c5 HAS_DRIVERS=1 TIKU_SHELL_ENABLE=1 \
    TIKU_THREADS_ENABLE=1 TIKU_ESP32C5_XIP_CODE=1 TIKU_DRV_WIFI_ESP_ENABLE=1 \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESP_PYTHON=/path/to/venv/bin/python ESPTOOL=/path/to/venv/bin/esptool
```

The Wi-Fi archives are pinned to `esp32-wifi-lib`
`af55a0ca258ce9d791d1661d7c2bbb65f08c0c21` and checked against
`SHA256SUMS-wifi`. The regulatory table is from the same pinned ESP-IDF
revision, including both bands. C5 AP records are 96 bytes, not the C61's 92.
Install the boot/XIP pair with `tools/esp32c5_flash.py`. Radio power stays off
until `wifi on`; `wifi scan` uses passive listening and `wifi off` stops workers
before returning the reserved heap. The default heap is 64 KiB of internal
SRAM: a passive scan of both bands keeps a 96-byte record per AP heard, and
48 KiB ran out at 154 APs, losing the table; 64 KiB peaked at 60 KiB with
232 APs, nothing refused. A second physical interrupt
line supports MAC and power sources that the vendor library routes to the
same logical interrupt.

Run the receive-and-restart qualification against that installed image:

```sh
python TikuBench/tools/esp32c5_wifi_check.py --port /dev/serial/by-id/your-c5-port \
    --json /path/to/new-c5-wifi-result.json
```

It refuses an already-active radio, performs three passive scan/start/stop
cycles, checks a stable MAC and full SRAM return, and leaves RF off. It does
not join a network, save credentials or include SSIDs in the report. An empty
scan fails receive qualification rather than passing just because its timer
completed. Traffic, RF performance and range need separate qualification.

Current qualification (2026-10-10, DevKitC-1, a dense office): a passive scan
of both bands finishes in 15 s with 154 APs, and 31 s with 208 to 215 APs
while BLE scans or advertises beside it; an open join lands on a 5 GHz
channel in 12 s and holds while BLE scans. The C5 profile carries no IP kit,
so association is as far as the shell goes on this chip.
These figures are with an antenna on the WROOM-1U's U.FL connector; without
one the same both-band scan returned a single AP or none.

## BLE

```sh
sh drivers/wifi/esp/c5/fetch.sh --ble
make MCU=esp32c5 HAS_DRIVERS=1 HAS_TIKUKITS=1 HAS_TESTS=0 HAS_EXAMPLES=0 \
    TIKU_SHELL_ENABLE=1 TIKU_THREADS_ENABLE=1 TIKU_ESP32C5_XIP_CODE=1 \
    TIKU_DRV_BLE_ESP_ENABLE=1 BUILD_DIR=build/esp32c5-ble \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESP_PYTHON=/path/to/venv/bin/python ESPTOOL=/path/to/venv/bin/esptool
```

The controller archive is `esp32c5-bt-lib`
`0b5cb2d7cfb4078e951da36a695bc4fb37e52391`. Its PHY/baseband archives use the
PHY revision above. `SHA256SUMS-ble` checks these and their licenses. C61
controller archives and ROM bindings must not be substituted.

Install the matching boot/XIP pair, then use `bt on`, `bt scan`, `bt list`,
`bt scan stop`, `bt advertise <name>`, `bt connect <scan-slot>`, `bt discover`,
`bt disconnect` and `bt off`. Scan slots start at 1. `bt uart <name>` exposes
the wireless shell; Ctrl-C on USB ends that session. The controller uses a
40 KiB SRAM heap and TikuOS worker threads; shutdown joins its deleted worker
before freeing the environment it references. The heap is returned on shutdown.

The controller uses the actual 40/48 MHz crystal selection, the 40 MHz AHB
clock and a 100 kHz low-power clock. The common PHY enables its Wi-Fi-power,
coexistence and analog-I2C clocks before calibration. A scoped APM master-4
mode change permits modem DMA into kernel SRAM; shutdown restores that mode.
It does not disable global access filters or replace locked security settings.

TikuBench's `bt` suite accepts `--board esp32c5` in both frontends. Hardware
checks cover HCI, identity, advertisement commands, actual scan reception,
VFS state and full SRAM recovery. The `bt-link` suite runs the C5 against
the ESP32-C61 both ways round: scan, connect, LE Secure Connections pairing
on the Security Request, GATT reads and uptime notifications, reconnect from
the bond, the bond kept across a reboot of both boards, the shell over the
Nordic UART Service and the beacon; 10 of 10 on 2026-10-09. Until then
pairing failed with Pairing Failed 0x08 from the C5: the host's entropy
wrapper was not compiled for this platform, and the C5 TRNG refused to run
while the PHY owned the analog bus (it now reads the RF-fed RNG register in
that state). The earlier BlueZ host runs, which dropped the link before GATT
discovery and logged malformed advertising report types, were that pairing
failure seen from the host side. With the antenna fitted (2026-10-10) the `bt`
suite passes 145 of 146: the board-initiated connection to a BlueZ peripheral
completes with GATT reads and notifications, while the shell over the Nordic
UART Service from the host fails at the host's scan, which never reports the
C5's advertisement although an nRF54L15 and the C61 receive it and the host
sees other advertisers; the host adapter's discovery is the suspect, not the
radio. The same round trip from an nRF54L15 as central passes by hand:
connect, the C5's Security Request answered with LE Secure Connections pairing
(the nRF's stack makes the P-256 keys itself), the bond stored, the link
encrypted, then subscribe, `info` written with its carriage return and the
answer back in notifications; the C5 needs tikukits 85e04cc for its pairing
entropy. Throughput and range are not qualified.

## Coexistence

```sh
sh drivers/wifi/esp/c5/fetch.sh --all
make MCU=esp32c5 HAS_DRIVERS=1 HAS_TIKUKITS=1 HAS_TESTS=0 HAS_EXAMPLES=0 \
    TIKU_SHELL_ENABLE=1 TIKU_THREADS_ENABLE=1 TIKU_ESP32C5_XIP_CODE=1 \
    TIKU_DRV_WIFI_ESP_ENABLE=1 TIKU_DRV_BLE_ESP_ENABLE=1 \
    BUILD_DIR=build/esp32c5-coex \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESP_PYTHON=/path/to/venv/bin/python ESPTOOL=/path/to/venv/bin/esptool
```

With both radios built, the C61's arbiter adapter (`../esp_coex.c`) serves
the C5 too: the adapter table is the same, the chip's crystal frequency is
read from the ROM's record, and `espw_arch_in_isr` is the C5's. The arbiter's
code and constants run from the XIP window; its two `.coexiram` routines stay
in SRAM, as on the C61. `fetch.sh --coex` pins `libcoexist.a` from
`esp-coex-lib` `c758e7b56e0fa22177a0539796e1df59978dc322`, `esp32c5/`,
checked by `SHA256SUMS-coex`; it is the C61's archive plus IEEE 802.15.4
hooks, with the same ROM symbol list. The two radios share one 96 KiB heap
(`TIKU_DRV_ESP_COEX_HEAP_BYTES`), taken by the first radio up: Wi-Fi's
packet buffers stay in SRAM on a module without PSRAM, and a both-band scan
beside BLE peaked at 85 KiB with nothing refused. `wifi on` reports
`[esp] coexistence 2.0.0` once per boot.

TikuBench's `coex` suite accepts `--board esp32c5`: both radios up, Wi-Fi
scans while BLE scans and advertises, the joined link held while BLE works
(with `--ssid`/`--psk`; no pings, the profile has no IP kit), and the heap
back whichever radio goes down first. The C5's both-band scan takes 31 s
under the arbiter, so the suite allows 45 s there.

## Receive-only SDR

```sh
make MCU=esp32c5 HAS_DRIVERS=1 HAS_TIKUKITS=0 HAS_TESTS=0 HAS_EXAMPLES=0 \
    TIKU_SHELL_ENABLE=1 TIKU_ESP32C5_XIP_CODE=1 TIKU_DRV_SDR_ESP_ENABLE=1 \
    BUILD_DIR=build/esp32c5-sdr \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESP_PYTHON=/path/to/venv/bin/python ESPTOOL=/path/to/venv/bin/esptool
```

This profile needs the PHY assets, not the Wi-Fi or BLE controller, and
enabling either radio with it is a build error. The
receiver reserves the entire 128 KiB bank at `0x40820000..0x4083ffff` before
powering the PHY. With no MAC running, the PHY's power bus is switched to its
debug mode and the receive chain powered by hand (`phy_pbus_xpd_rx_on`); in
work mode the MAC's state machine would leave the receiver off and every
sample would be one DC value. In debug mode the baseband's forced gain index
does not reach the RF chain, so `sdr gain` does not change the receiver's
gain and captures report a gain of 0; the `sdr` suite records this as a
skip. Samples occupy its upper 64 KiB, starting at `0x40830000`.
The linker refuses a firmware whose allocator cannot own that whole bank.
Startup also checks the allocated address. Do not weaken either check to fit
more services: the dump engine takes hardware ownership of the entire bank.

```text
sdr info
sdr bands
sdr start
sdr gain 60
sdr cap 2440 1 1024
sdr hex 0 64
sdr spec 2440 1 128
sdr sweep 2430 2450 10 1 128
sdr spec 5180 1 128
sdr stop
```

Rate codes 0–5 select 80, 40, 20, 10, 8 and 4 MS/s. A snapshot accepts
1–16380 complex samples; FFT sizes are 64, 128 and 256. Each raw word carries
signed ten-bit I and Q plus gain metadata. Requested receive ranges are
2400–2500 and 4900–5900 MHz. Gain is a PHY table index, not calibrated dB;
`sdr gain auto` selects hardware gain control. Fixed gain is capped at the
PHY's limit and the 90-entry table's boundary.

Capture rejects another PHY or DMA owner, malformed arguments, timeouts,
unwritten samples, damaged guards and completely constant I/Q. It uses a
finite transfer and stops the writer before restoring SRAM ownership. The
poll is bounded by 20 ms and an iteration cap, including across timer wrap.
Interrupts are masked during the transfer, so this is a snapshot receiver,
not a continuous stream or a concurrent-radio service. `sdr stop` powers off
the PHY and returns the full bank; cleanup failures retain the reservation.

The `SPEC` protocol supports TikuSDR's spectrum viewer. `sdr bands` reports
both C5 bands separately. The desktop offers reported 5 GHz channel presets
and typed frequencies, rejects sweeps across the band gap, and records and
replays either band. Its occupancy plans remain 2.4 GHz-only. The legacy
`sdr info` tuning range remains 2400–2500 for compatibility. Transmit probe
builds are refused.

TikuBench's `sdr` suite checks both bands at all six rates, three FFT sizes,
manual/automatic gain, the largest capture, sweeps, invalid inputs and three
complete memory-return cycles. These are data-path tests, not RF calibration.
At 5 GHz, high gain produces a large DC bias and can clip; the pinned SDK
PHY-only reference reproduces this behavior. Absolute power, sensitivity,
frequency response and 5 GHz calibration require a controlled RF source.

```sh
PYTHONPATH=TikuBench python -m tikubench run sdr --board esp32c5 \
    --port /dev/serial/by-id/your-c5-port --skip-build --json /path/to/result.json
```

Use `bt` instead of `sdr` for the separately installed BLE profile. Neither
suite silently powers down a radio already owned by another user.

## IEEE 802.15.4

```sh
sh drivers/wifi/esp/c5/fetch.sh --ble
make MCU=esp32c5 HAS_DRIVERS=1 HAS_TIKUKITS=1 HAS_TESTS=0 HAS_EXAMPLES=0 \
    TIKU_SHELL_ENABLE=1 TIKU_ESP32C5_XIP_CODE=1 TIKU_DRV_154_C5_ENABLE=1 \
    EXTRA_CFLAGS=-DTIKU_SHELL_CMD_RADIO154=1 BUILD_DIR=build/esp32c5-154 \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESP_PYTHON=/path/to/venv/bin/python ESPTOOL=/path/to/venv/bin/esptool
```

`ieee154_c5.c` implements `hal/tiku_ieee154_hal.h` under the MAC in
`interfaces/radio/` and the `radio154` command, from the C5's 15.4 MAC block's
registers (ESP-IDF's open `ieee802154` driver is the reference; no vendor 15.4
library is used).  The profile takes the PHY alone: building it with Wi-Fi,
BLE or the SDR is a build error.  It needs `libbtbb.a` from the BLE download
for the baseband shared with BLE, the MAC's ramp delays and the TX power
table.  Taking the radio turns the PHY on, opens the 15.4 clock domain (the
15.4 MAC and APB, ETM, modem-security APB, BT APB and baseband clocks and the
15.4 gate map), resets the MAC, turns arbitration off and sets CCA by energy
at -75 dBm and 0 dBm transmit power; leaving closes the PHY and then restores
those fields.  Events are polled.

Energy detection measures for 1 ms (64 symbols, the maximum over the window)
and reports the level on the nRF54L's scale, dBm + 94; CCA measures 8 symbols.
`rx` receives promiscuously without ACKs into a DMA buffer the MAC fills as
[PHR][frame][RSSI][LQI]; a bad FCS ends the listen as abort reason 3.
`rx_ack` filters on interface 0's PAN and short address and lets the MAC block
send the ACK; the MAC layer above waits for ACKs to its own frames through
`rx`.  Link security uses the software AES-CCM* in `tikukits/crypto/ccm/`.

Two timings come from the board, not from ESP-IDF.  After sending an ACK the
MAC turns back to receive, and a stop inside that turnaround makes its next
ACK time out (abort reason 16); the driver waits 300 us after each ACK.  And
after a stop the RF chain needs a few microseconds to wind down: closing the
PHY inside that window left every later bring-up unable to measure or receive
until the chip was reset (no pause failed from the third bring-up on, 5 us
never did); `leave` waits 50 us.  Both failures depended on code layout,
because a flash-cache miss in the path supplied the missing time.

Against an nRF54L15 at about 20 cm (2026-10-10): its pings on channel 20 read
-41 dBm on channel 20 and the floor on channel 21; five frames each way
arrived intact on channels 11, 15 and 26 (C5 at -44 to -53 dBm, nRF at -56 to
-61 dBm) and none on channel 16 while it sent on 15; and TikuBench's
`radio154` suite with the C5 as DUT passes 6 of 6 on channel 15: 200/200
unicasts ACKed each way and 150/150 secured frames verified each way.

## Calls and ownership

Call `tiku_drv_phy_c5_on()` from kernel foreground with interrupts enabled.
The first call performs full calibration; later calls restore the PHY's
calibration state. Repeating `on()` while active does not add a reference.
Call `tiku_drv_phy_c5_off()` to close RF and restore the digital clock fields
that the driver changed. Repeating `off()` is harmless. The driver reports
the vendor calibration result, successful calibration count, and duration.

The PHY claims the shared analog subsystem before changing its clocks.
While claimed, SAR-based random-number reads return NOT_READY and clear the
output, and live CPU-divider changes are refused. Existing ADC/temperature
activity prevents the PHY claim. The PHY returns ownership after shutdown.
The PMU analog-bus power commands remain enabled, following the reference
clock setup; this API does not promise a measured radio-off current budget.

The C5 initialization data is 256 bytes and uses its C5 format terminator.
Its rate-group power ceilings are 10 dBm. This is not regulatory certification
or a Wi-Fi country/channel policy. Calibration may emit RF. The PHY-only
build does not start a MAC, scan, associate, advertise or track temperature
automatically. The optional Wi-Fi adapter starts the MAC and adds periodic
PHY tracking. BLE uses its own controller adapter. There is no C5 raw-radio
transmit API.

The vendor calibration and wake calls are blocking. On a calibration error
the driver latches a fault, does not report active, and retains ownership;
reboot before another attempt. These calls are not a bounded-time API.

## Tests

```sh
make -C TikuBench/tests/host/kernel esp32c5-boot
PYTHONPATH=TikuBench python -m tikubench run kernel --board esp32c5 \
    --console usb --port /dev/serial/by-id/your-c5-port \
    --only phy-c5 --flash \
    --make-var TOOLCHAIN_DIR=/path/to/riscv-toolchain \
    --make-var TOOLCHAIN_PREFIX=riscv-none-elf- \
    --make-var ESPTOOL=/path/to/venv/bin/esptool
```

`phy-c5` is an explicit diagnostic, excluded from an ordinary kernel sweep.
It checks calibration and warm restart at the supported CPU rates, analog
ownership, IRQ-call rejection, digital clock restoration, and entropy after
shutdown. Host models cover failed clocks/calibration, reentry, worker
rejection, initialization-data layout and repeated lifecycle calls, including
mutations that must fail. The package-memory check reports the read-only
PSRAM eFuse code, not mapped or allocator-available capacity.

The PHY-only tests do not establish RF sensitivity, transmit power, off-state
current, Wi-Fi connectivity, BLE functionality or PSRAM data integrity.
