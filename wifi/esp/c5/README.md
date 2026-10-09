# ESP32-C5 native radio adapters

This component controls the C5 radio's physical layer: initial calibration,
warm wake, and RF shutdown. An optional Wi-Fi build uses C5 vendor libraries
with the shared TikuOS OS adapter and C5 interrupt, clock and timer bindings.
There is no ESP-IDF or FreeRTOS runtime dependency. Separate opt-in profiles
provide BLE and receive-only SDR. IEEE 802.15.4 and simultaneous-radio
coexistence are not provided.

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
before returning the reserved heap. The default heap is 48 KiB of internal
SRAM. A second physical interrupt line supports MAC and power sources that the
vendor library routes to the same logical interrupt.

Run the receive-and-restart qualification against that installed image:

```sh
python TikuBench/tools/esp32c5_wifi_check.py --port /dev/serial/by-id/your-c5-port \
    --json /path/to/new-c5-wifi-result.json
```

It refuses an already-active radio, performs three passive scan/start/stop
cycles, checks a stable MAC and full SRAM return, and leaves RF off. It does
not join a network, save credentials or include SSIDs in the report. An empty
scan fails receive qualification rather than passing just because its timer
completed. Association, traffic, both-band coverage, RF performance and
coexistence need separate qualification.

Current qualification: native start/stop and scan completion work, with full
SRAM return, but captured native scans have not received APs. The pinned SDK
control received one AP in early scans and later returned empty scans too,
including its default configuration. That is insufficient to qualify native
reception or assign a confirmed cause. Association and packet traffic are not
tested. Keep this opt-in adapter experimental until reception is reproduced
against a nearby controlled AP and the board's antenna is checked.

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
VFS state and full SRAM recovery. A C5 central connection to a temporary
BlueZ peripheral also exchanged GATT reads and advancing uptime notifications
over the air (`bt --only 27`). Short peer-discovery runs were inconsistent;
received host RSSI ranged down to -95 dBm. A passing exchange does not
establish reliable range or sustained throughput.
Later runs also connected and then received a remote disconnect before GATT
discovery. BlueZ retained `Connected=yes` while the C5 radio was off and the
host's HCI connection list was empty. That test environment needs recovery
before a repeatability claim; do not suppress failed peer tests.
The host-to-C5 UART test remains a separate qualification: runs fail at
advertisement discovery or connection. This host logs malformed advertising
report types; discovery also failed with a pinned SDK reference image. These
observations do not establish the cause or qualify C5 peripheral operation.
Pairing/encryption, throughput and Wi-Fi/BLE coexistence are not qualified.
Enabling Wi-Fi and BLE together is a build error.

## Receive-only SDR

```sh
make MCU=esp32c5 HAS_DRIVERS=1 HAS_TIKUKITS=0 HAS_TESTS=0 HAS_EXAMPLES=0 \
    TIKU_SHELL_ENABLE=1 TIKU_ESP32C5_XIP_CODE=1 TIKU_DRV_SDR_ESP_ENABLE=1 \
    BUILD_DIR=build/esp32c5-sdr \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESP_PYTHON=/path/to/venv/bin/python ESPTOOL=/path/to/venv/bin/esptool
```

This profile needs the PHY assets, not the Wi-Fi or BLE controller. The
receiver reserves the entire 128 KiB bank at `0x40820000..0x4083ffff` before
powering the PHY. Samples occupy its upper 64 KiB, starting at `0x40830000`.
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
