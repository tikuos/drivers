# ESP32-C61 Wi-Fi (Espressif radio libraries)

Wi-Fi for tikuOS on the ESP32-C61 through an OS shim over Espressif's
binary radio libraries -- the approach NuttX and Zephyr take. The design
note, milestones and decisions live in `kintsugi/esp32c61-radio-plan.md`.

## Status

| Milestone | Deliverable | State |
|-----------|-------------|-------|
| R0 | Libraries link; their code runs from flash (XIP) | done: boot prints the PHY version from flash-resident code |
| R1 | PHY + MAC up: RF calibration, MAC address, init/start OK | pending |
| R2 | Scan through `tiku_wireless` | pending |
| R3 | Join: open network, then WPA2-PSK (clean-room supplicant) | pending |
| R4 | DHCP, ping, UDP/HTTP; TikuBench net rows | pending |
| R5 | Radio off: sleep numbers unchanged | pending |
| R6 | BLE | later |

## Fetching the libraries

Nothing binary is tracked in git. `fetch.sh` downloads into `vendor/`
(ignored) the files ESP-IDF 4d59230 pins, then checks them against
`SHA256SUMS`:

```
sh drivers/wifi/esp/fetch.sh
```

| File | From | Licence |
|------|------|---------|
| libnet80211.a, libpp.a, libcore.a | espressif/esp32-wifi-lib @ af55a0c, esp32c61/ | Apache-2.0 |
| libphy.a | espressif/esp-phy-lib @ 20f1db0, esp32c61/ | Apache-2.0 |
| esp32c61.rom{,.api,.coexist,.net80211,.pp,.phy,.version}.ld | espressif/esp-idf @ 4d59230, components/esp_rom/esp32c61/ld | Apache-2.0 |

The ROM scripts only name addresses in the chip's ROM, where much of the
Wi-Fi stack lives. `build.mk` stops with a pointer here when any file is
missing.

## Build and flash

```
make MCU=esp32c61 TIKU_SHELL_ENABLE=1 TIKU_DRV_WIFI_ESP_ENABLE=1
make flash MCU=esp32c61 TIKU_SHELL_ENABLE=1 TIKU_DRV_WIFI_ESP_ENABLE=1
```

The libraries' code is too large for the 320 KB SRAM the kernel runs from,
so `esp_xip.ld` places it in the XIP window: flash 1 MB on, read through the
1:1 map the port already sets. The image splits in two -- `main.bin` at
flash 0 as always, and `xip.bin` at 0x100000 -- and `make flash` writes
both. `xip.bin` opens with a header naming where the SRAM image's text and
bss end; at boot the driver refuses an `xip.bin` from another build instead
of calling into it:

```
[esp-wifi] xip.bin in flash is not this build's -- make flash writes both images
```

At R0 the image is 143 KB of SRAM image plus 299 KB in flash; the
libraries add about 25 KB of code (`.iram1`) and 15 KB of data to SRAM.
BIG BASIC and the radio do not fit SRAM together yet.

## Files

- `tiku_drv_wifi_esp.c/.h` -- the driver descriptor and the XIP check
- `esp_abi.h` -- the libraries' ABI this driver uses, hand-written
- `esp_glue.c` -- the symbols the libraries expect around them
- `esp_xip.ld` -- the fragment placing the libraries in the XIP window
- `fetch.sh`, `SHA256SUMS` -- download and verification
