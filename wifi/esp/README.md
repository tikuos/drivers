# ESP32-C61 Wi-Fi (Espressif radio libraries)

Wi-Fi for tikuOS on the ESP32-C61 through an OS shim over Espressif's
binary radio libraries -- the approach NuttX and Zephyr take. The design
note, milestones and decisions live in `kintsugi/esp32c61-radio-plan.md`.

## Status

| Milestone | Deliverable | State |
|-----------|-------------|-------|
| R0 | Libraries link; their code runs from flash (XIP) | done: boot prints the PHY version from flash-resident code |
| R1 | PHY + MAC up: RF calibration, MAC address, init/start OK | done: `wifi on` calibrates in 66 ms, starts the station, reports the MAC |
| R2 | Scan through `tiku_wireless` | done: `wifi scan` finds the APs around the bench (30-36 in 2.4 s) |
| R3 | Join: open network, then WPA2-PSK (clean-room supplicant) | done: joins an open network in 2.5 s; the WPA2 handshake proved against a scripted AP on the host |
| R4 | DHCP, ping, UDP/HTTP; TikuBench net rows | in part: DHCP, DNS and ping over the radio (TikuBench wifi tests 12-14); HTTPS builds and runs to the network with code in flash and buffers in PSRAM -- live sites wait on an open network |
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

For IP over the radio add the lean net stack (TikuBench's wifi firmware for
this board builds the same):

```
TIKU_KIT_NET_ENABLE=1 TIKU_KIT_NET_MIN=1 TIKU_KITS_NET_WIFI_ENABLE=1
TIKU_KITS_NET_DHCP_ENABLE=1 TIKU_KITS_NET_DNS_ENABLE=1
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

The SRAM image is 221 KB (the libraries' `.iram1` code and data, the
supplicant and its crypto included), 236 KB with the IP stack, and
`xip.bin` 381 KB.  BASIC with HTTPS does not fit SRAM beside the radio
as it stands: build it with `TIKU_ESP32C61_XIP_CODE=1` (BASIC, the shell's
commands, the IP stack, TLS and crypto run from flash) and
`TIKU_ESP32C61_PSRAM_DATA=1` (TLS's and BASIC's big buffers in PSRAM) -- 135
KB of SRAM image then, a 79 KB SRAM tier.  HTTPGET$ needs the trust store in
`/data/roots.bin` (`tools/gen_roots.py`).

## Using it

The radio is off until asked, and costs nothing while off:

```
tikuOS:/> wifi on
[esp-wifi] station started
[esp-wifi] RF calibrated: 0 in 66 ms
[esp-wifi] up: MAC 30:ed:a0:e7:ee:d4
[esp-wifi] heap: 30296 of 49152 bytes in use, 30552 at most
tikuOS:/> wifi scan
[esp-wifi] *** scan done -- 30 APs in 2445 ms ***
tikuOS:/> wifi list
tikuOS:/> wifi connect "MyNetwork" mypassphrase
[esp-wifi] *** LINK UP -- joined MyNetwork (channel 9) in 2507 ms ***
tikuOS:/> wifi up
tikuOS:/> ip
IPv4: 172.25.114.161
Mask: 255.255.254.0
Gateway: 172.25.114.1
DNS: 172.19.215.140
Lease: 900 s
reachable now -- on WiFi
tikuOS:/> ping 172.25.114.1
tikuOS:/> wifi disconnect
[esp-wifi] *** LINK DOWN -- left MyNetwork ***
tikuOS:/> wifi off
[esp-wifi] down: the heap peaked at 32600 of 49152 bytes, 0 refused
```

`wifi on` takes 48 KB from the SRAM tier for the libraries' heap (30 KB in
use at rest, 36 KB at most measured joined with IP traffic; `wifi off` says
how high it went), starts a
timer thread and the libraries' own task (two worker slots), calibrates the
RF the first time (later starts wake the PHY from what it kept), and starts
the station.  `wifi off` stops it all and gives the heap back once it is
empty.  The libraries keep a few locks from one start to the next; those
sit in a small static pool so that the heap can empty.  `wifi status` and
`/proc/wifi/` show the state, the MAC and the radio's interrupts.

`wifi scan` scans every channel the country allows (the world-safe
default, 1-11), and `wifi list` shows the 16 strongest of what it found.
The stack consults a supplicant even to scan, for the RSN and WPA
elements of each AP.  The country table in `esp_glue.c` is a hand-written
handful of countries' 2.4 GHz rules.

`wifi connect SSID PSK` joins the strongest AP of that name: a WPA2-PSK
network, or an open one for an empty passphrase (`wifi connect SSID ""`).
The passphrase becomes its PMK (PBKDF2, 4096 rounds) in the driver and is
wiped; a failed join is tried three times in all, and a link that drops is
rejoined the same way; `wifi disconnect` leaves for good.  `wifi status`
and `/proc/wifi/` show the network, the AP's signal and the join time.

The supplicant, `esp_wpa.c`, is tikuOS's own, written from IEEE
802.11-2020 12.7 over TikuKits' HMAC-SHA1 and AES key wrap: the 4-way
handshake, then group rekeys.  It joins WPA2-PSK with CCMP both ways and
nothing else: no TKIP, no WPA3 (SAE), no PMF -- the driver turns PMF off
for each join, and an AP that requires it is not joined.  The keys go to
the radio only once message 4 has left, and never twice (no key
reinstallation).  `TikuBench/tests/host/nonkernel/test_wpa.c` runs it
against a scripted AP on the host (`make -C TikuBench/tests/host/nonkernel
wpa`), the expected keys computed apart from it.  `esp_crypto.c` gives the
libraries the crypto they call themselves; the PMF entries refuse.

`wifi up` puts the IP stack on the radio and asks for a DHCP lease; `ip`
then shows the address, mask, gateway, DNS server and lease, and `ping`,
`dns` and `ntp` ride the radio.  Frames go out through
`tiku_wireless_tx_eth` (the libraries copy them) and come in on the
libraries' task, which queues each buffer for the runner: the IP stack sees
them in the kernel thread, and each buffer goes back once delivered -- or at
once, when nothing listens.

Bring-up tracing (every blocking wait, task and interrupt route) is
compiled in with `EXTRA_CFLAGS=-DESPW_TRACE=1`.

## Files

- `tiku_drv_wifi_esp.c/.h` -- the driver descriptor, the XIP check, the
  radio's on/off, joining, the frame path and the `tiku_wireless` interface
- `esp_osi.c` -- the OS the libraries run on: locks and queues over kernel
  wait queues, tasks as worker threads, timers on SYSTIMER's driver alarm,
  their interrupt lines on the radio's CLIC lines
- `esp_phy.c` -- the modem's clocks, the PHY's bring-up and calibration,
  the MAC address
- `esp_heap.c/.h` -- the libraries' heap and lock pool
- `esp_wpa.c` -- the supplicant the stack calls: RSN/WPA element parsing,
  the WPA2-PSK handshakes, the keys to the radio
- `esp_crypto.c` -- the crypto table the libraries call, over TikuKits
- `esp_port.h` -- what those files share
- `esp_abi.h` -- the libraries' ABI this driver uses, hand-written
- `esp_glue.c` -- the symbols the libraries expect around them
- `esp_xip.ld` -- the fragment placing the libraries in the XIP window
- `fetch.sh`, `SHA256SUMS` -- download and verification
