# ESP32-C5 native radio adapters

This component controls the C5 radio's physical layer: initial calibration,
warm wake, and RF shutdown. An optional Wi-Fi build uses C5 vendor libraries
with the shared TikuOS OS adapter and C5 interrupt, clock and timer bindings.
There is no ESP-IDF or FreeRTOS runtime dependency. IEEE 802.15.4 and simultaneous-radio
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
executes from internal SRAM; the Wi-Fi build requires XIP for ordinary vendor
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
PHY tracking. There is no C5 raw-radio
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
