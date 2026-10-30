# ESP32-C6 native Wi-Fi

The C6 adapter runs Espressif's station libraries on TikuOS workers, queues,
locks, allocator and timers. Application execution uses the native C6 kernel.
The board's preserved SDK application provides the update/recovery path.

## Build

Fetch the target-specific archives, ROM scripts and licenses:

```sh
sh drivers/wifi/esp/c6/fetch.sh
```

`ASSETS.json` records immutable source URLs, file lengths and SHA-256 hashes.
`SHA256SUMS` is checked at build time. Vendor downloads are ignored by git.
The assets are Apache-2.0 and use the versions pinned by ESP-IDF
`4d59230ddff16327812782151ef0afef202dc6d7`.

```sh
make MCU=esp32c6 C6_PROFILE=firmware C6_STORAGE=1 \
    TIKU_SHELL_ENABLE=1 TIKU_SHELL_BASIC_ENABLE=1 \
    TIKU_DRV_WIFI_ESP_ENABLE=1 TIKU_KIT_NET_ENABLE=1 C6_BUILD=1 \
    firmware-image C6_RECOVERY_IMAGE=/path/to/tiku_c6_access.bin
```

Install the application, ELF and memory report with the checked C6 installer
described in the [native port README](../../../../arch/esp32c6/README.md).
The Wi-Fi profile requires storage, the shell and workers. It rejects BLE
and SDR. Leave out `TIKU_KIT_NET_ENABLE=1` for station operation without IP.
The IP profile includes IPv4, ICMP, UDP, TCP, DHCP and the Wi-Fi link adapter;
TCP buffers reside in SRAM. DNS and NTP shell commands are disabled in this
profile.

## Hardware bindings

The shared OS table uses ABI version 9. C6 SDK measurements give 508 bytes for
`wifi_osi_funcs_t`, 160 bytes for `wifi_init_config_t`, 92 bytes for
`wifi_ap_record_t`, 128 bytes for PHY initialization and 1904 bytes for PHY
calibration. The shared ABI header checks RV32 sizes and offsets.

SYSTIMER target 2 and PLIC lines 24–31 belong to the radio adapter while it
runs. The kernel retains target 1/line 17 and its worker switch on line 18;
the hardware alarm HAL uses target 0/line 19. The adapter refuses occupied
interrupt routes. It restores its saved digital modem fields after the
PHY stops and workers exit. Analog power consumption after shutdown has
not been measured.

A station session takes a 48 KiB SRAM workspace. PHY calibration data
is allocated within that workspace and freed after calibration. The adapter
requests full calibration on the first start after CPU reset; measured calls
take 88–90 ms. The PHY's wake entry handles subsequent starts, including
starts after radio shutdown. Transmit power parameters
are capped at 20 dBm. Output power has not been calibrated at the antenna.
The general RNG uses
RF entropy while the calibrated PHY owns the analog bus; ADC transactions
refuse access while the modem front end is clocked.

The native memory routines use word accesses for aligned copies into the
hardware key RAM. The vendor's direct `free` calls return temporary key
buffers to the radio allocator. The supplicant supplies a retained RSN
buffer with the writable two-byte length prefix required by the library.

## Qualification

The FireBeetle 2 P4's C6 revision 0.2 has passed native station startup,
factory MAC reporting, scanning and shutdown. A scan found 17 APs in 2421 ms.
Full RF calibration took 90 ms. The radio heap peaked at 32,936 of 49,152 bytes
while scanning, with zero refused allocations; shutdown returned the SRAM
workspace. These observations are board captures, not RF sensitivity or
throughput measurements.

The native BASIC/IP build fits a 400 KiB arena with 99,840 bytes of allocatable
SRAM, 32 KiB worker reservation, 64 KiB radio reservation, 2 KiB interrupt stack
and 8 KiB main stack. Its initialized SRAM image is 108,180 bytes and XIP
payload 536,580 bytes. Build-specific reports remain authoritative.

Host tests cover radio interrupt admission and restoration, modem/PHY
ownership, RF entropy and ADC exclusion, calibration and wake/stop cycles,
aligned memory copies and byte tails under sanitizers, rejected source
mutations, application/XIP bounds and expired backup-read leases. Run:

```sh
make -C TikuBench/tests/host/kernel c6-radio c6-peripherals
python3 -B TikuBench/tests/host/kernel/test_esp32c6_firmware.py
```

Station authentication uses the shared independent supplicant: open and
WPA2-PSK with CCMP. WPA3-SAE, TKIP and required PMF are refused. A controlled
ESP32-C5 AP on channel 6 has passed open and WPA2 association, DHCP and checked
UDP/TCP traffic in both directions: 4,096 bytes per UDP flow and 8,192 bytes
per TCP flow, with no payload errors or application retries. Deliberately
corrupted UDP data is rejected. These stop-and-wait flows check data delivery;
they do not measure link capacity.

WPA2 qualification covers three radio off/on joins, wrong-password and
missing-AP refusal followed by successful joins, automatic reassociation
after an AP restart, and an explicit join after C6 reset. DHCP succeeds after
each recovery. Shutdown returns the radio workspace with no live workers,
stack-canary faults or native errors.
Thailand uses channels 1–13 with the pinned SDK's 20 dBm regulatory cap.
Normal firmware has passed 100 hardware start/stop cycles
with zero live workers, stack-canary faults or native errors after shutdown.
The SRAM workspace returns after each cycle, with zero allocation refusals.
No C6 BLE, IEEE 802.15.4 or SDR qualification is implied by Wi-Fi startup.
