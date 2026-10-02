# ESP32-C61 Wi-Fi (Espressif radio libraries)

Wi-Fi for tikuOS on the ESP32-C61 through an OS shim over Espressif's
binary radio libraries -- the approach NuttX and Zephyr take. The design
note, milestones and decisions live in `kintsugi/esp32c61-radio-plan.md`.

## Status

| Milestone | Deliverable | State |
|-----------|-------------|-------|
| R0 | Libraries link; their code runs from flash (XIP) | done: boot prints the PHY version from flash-resident code |
| R1 | PHY + MAC up: RF calibration, MAC address, init/start OK | done: `wifi on` calibrates in 66 ms, starts the station, reports the MAC |
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

The radio's tasks run as worker threads, so the build needs them:

```
make MCU=esp32c61 TIKU_SHELL_ENABLE=1 TIKU_THREADS_ENABLE=1 TIKU_DRV_WIFI_ESP_ENABLE=1
make flash MCU=esp32c61 TIKU_SHELL_ENABLE=1 TIKU_THREADS_ENABLE=1 TIKU_DRV_WIFI_ESP_ENABLE=1
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

The SRAM image is 207 KB (the libraries' `.iram1` code and data included)
and `xip.bin` 365 KB.  BIG BASIC and the radio do not fit SRAM together
yet.

## Using it

The radio is off until asked, and costs nothing while off:

```
tikuOS:/> wifi on
[esp-wifi] station started
[esp-wifi] RF calibrated: 0 in 66 ms
[esp-wifi] up: MAC 30:ed:a0:e7:ee:d4
[esp-wifi] heap: 30176 of 57344 bytes in use, 30432 at most
tikuOS:/> wifi status
tikuOS:/> wifi off
```

`wifi on` takes 56 KB from the SRAM tier for the libraries' heap, starts a
timer thread and the libraries' own task (two worker slots), calibrates the
RF the first time (later starts wake the PHY from what it kept), and starts
the station.  `wifi off` stops it all and gives the heap back once it is
empty.  The libraries keep a few locks from one start to the next; those
sit in a small static pool so that the heap can empty.  `wifi status` and
`/proc/wifi/` show the state, the MAC and the radio's interrupts.

Bring-up tracing (every blocking wait, task and interrupt route) is
compiled in with `EXTRA_CFLAGS=-DESPW_TRACE=1`.

## Files

- `tiku_drv_wifi_esp.c/.h` -- the driver descriptor, the XIP check, the
  radio's on/off and the `tiku_wireless` interface
- `esp_osi.c` -- the OS the libraries run on: locks and queues over kernel
  wait queues, tasks as worker threads, timers on SYSTIMER's driver alarm,
  their interrupt lines on the radio's CLIC lines
- `esp_phy.c` -- the modem's clocks, the PHY's bring-up and calibration,
  the MAC address
- `esp_heap.c/.h` -- the libraries' heap and lock pool
- `esp_port.h` -- what those files share
- `esp_abi.h` -- the libraries' ABI this driver uses, hand-written
- `esp_glue.c` -- the symbols the libraries expect around them
- `esp_xip.ld` -- the fragment placing the libraries in the XIP window
- `fetch.sh`, `SHA256SUMS` -- download and verification
