# ESP32-C61 Wi-Fi and BLE (Espressif radio libraries)

Wi-Fi and Bluetooth LE for tikuOS on the ESP32-C61 through an OS shim over
Espressif's binary radio libraries -- the approach NuttX and Zephyr take.
The design notes, milestones and decisions live in
`kintsugi/esp32c61-radio-plan.md` and `kintsugi/esp32c61-ble-plan.md`.

## Status

| Milestone | Deliverable | State |
|-----------|-------------|-------|
| R0 | Libraries link; their code runs from flash (XIP) | done: boot prints the PHY version from flash-resident code |
| R1 | PHY + MAC up: RF calibration, MAC address, init/start OK | done: `wifi on` calibrates in 66 ms, starts the station, reports the MAC |
| R2 | Scan through `tiku_wireless` | done: `wifi scan` finds the APs around the bench (30-36 in 2.4 s) |
| R3 | Join: open network, then WPA2-PSK (clean-room supplicant) | done: joins an open network in 2.5 s; the WPA2 handshake proved against a scripted AP on the host |
| R4 | DHCP, ping, UDP/HTTP; TikuBench net rows | in part: DHCP, DNS and ping over the radio (TikuBench wifi tests 12-14); HTTPS builds and runs to the network with code in flash and buffers in PSRAM -- live sites wait on an open network |
| R5 | Radio off: sleep numbers unchanged | pending |
| R6 | BLE: the LE controller under tikuOS's own host stack | in part: `bt on` brings the controller up (HCI Reset, version, address through the host), `bt scan` finds the advertisers around the bench, `bt advertise` runs; TikuBench bt 114/114 on the C61; Wi-Fi and BLE run together through Espressif's coexistence arbiter; connections need a second device |

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
| libphy.a, libbtbb.a | espressif/esp-phy-lib @ 20f1db0, esp32c61/ | Apache-2.0 |
| libble_app.a | espressif/esp32c6-bt-lib @ a00f2d0, esp32c61/ | Apache-2.0 |
| libcoexist.a | espressif/esp-coex-lib @ c758e7b, esp32c61/ | Apache-2.0 |
| esp32c61.rom{,.api,.coexist,.net80211,.pp,.phy,.version}.ld | espressif/esp-idf @ 4d59230, components/esp_rom/esp32c61/ld | Apache-2.0 |

The ROM scripts only name addresses in the chip's ROM, where much of the
Wi-Fi stack lives. `build.mk` stops with a pointer here when any file is
missing.

## Build and flash

The radios' tasks run as worker threads, so the build needs them.  Wi-Fi:

```
make MCU=esp32c61 TIKU_SHELL_ENABLE=1 TIKU_THREADS_ENABLE=1 TIKU_DRV_WIFI_ESP_ENABLE=1
make flash MCU=esp32c61 TIKU_SHELL_ENABLE=1 TIKU_THREADS_ENABLE=1 TIKU_DRV_WIFI_ESP_ENABLE=1
```

BLE, with `TIKU_DRV_BLE_ESP_ENABLE=1` in place of (or beside) the Wi-Fi
flag.  Both together need `TIKU_ESP32C61_XIP_CODE=1` to fit SRAM, and then
share one heap and Espressif's coexistence arbiter (see Coexistence).

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

## BLE

Espressif's LE controller carries tikuOS's own host stack
(`tikukits/net/bluetooth`, the one the Pico 2 W's CYW43 uses) over its
in-memory HCI:

```
tikuOS:/> bt on
[esp-ble] up: address 30:ed:a0:e7:ee:d6, NPL 66/1/18/0/0
[bt] p6.D: HCI_Reset OK (status=0x00)
[bt] p6.D: Read_Local_Version hci=14 lmp=14 mfr=0x02e5 sub=0x0000
tikuOS:/> bt scan
tikuOS:/> bt list
tikuOS:/> bt advertise TikuC61
tikuOS:/> bt off
[esp-ble] down: the heap peaked at 32536 of 40960 bytes, 0 refused, 0 packets dropped
```

`bt on` takes 40 KB from the SRAM tier for the controller's heap (28 KB
in use at rest, 33 KB scanning and advertising at once), registers what
the controller calls --
`esp_npl.c`, its OS (events, queues, callouts, locks) over kernel wait
queues and the shim's timer service, and `esp_mempool.c`, the memory pools
it imports -- clocks the BLE MAC, and initialises and enables the
controller in the order IDF does.  The controller's code runs from flash,
as IDF's run-in-flash-only mode places it, with that mode's relaxed timing;
only its 2 KB of hot paths and its data stay in SRAM.  Its configuration is
IDF's default cut to what the host uses -- one link, a 23-byte ATT MTU,
legacy advertising: 8 high-priority event buffers, 4 ACL buffers of 255
bytes, 251-byte advertising data, msys 8 x 256 + 8 x 320 -- a quarter less
memory than IDF's.  The address is the factory MAC's third universal one
(last byte + 2).  `bt off` stops it all and gives the heap back.

Its task is a worker thread, and the kernel thread runs first: the driver
waits for the controller's first HCI NOP before the host starts, and lets
its task finish before each command (it frees a command's buffer only
after the reply).  `bt status` shows the heap and the radio's interrupts.
`EXTRA_CFLAGS=-DESPB_TRACE=1` traces every HCI command, event and receive.

## Coexistence

With both radios built, Espressif's arbiter -- `libcoexist.a`, most of it
in the C61's ROM -- grants the one RF front end to one stack at a time.
`esp_coex.c` registers its adapter (semaphores, timers and heap, all from
the shim) once per boot as the first radio comes up; Wi-Fi reaches it
through its OS table, BLE through its coexistence hooks.  The arbiter
allocates its 56-byte function table once and the ROM keeps the pointer, so
that comes from a static pool and the heap can still empty.

The two radios share one SRAM heap, 60 KB
(`TIKU_DRV_ESP_COEX_HEAP_BYTES`), taken by the first radio up and given
back after the last down.  Wi-Fi's packet buffers come from a 96 KB block
of PSRAM instead (`TIKU_DRV_WIFI_ESP_PSRAM_BYTES`, 0 in a Wi-Fi-only
build), as IDF's SPIRAM_TRY_ALLOCATE_WIFI_LWIP places them; PSRAM comes up
on demand.  Joined to an AP with BLE scanning or advertising, pings come
back 4 of 4; the shared heap peaked at 48 KB, and both orders of `off`
give it all back.

## The receiver (raw I/Q)

With `TIKU_DRV_SDR_ESP_ENABLE=1` beside the Wi-Fi driver (and
`TIKU_ESP32C61_XIP_CODE=1`), the radio is also a 2.4 GHz receiver whose raw
samples the CPU sees.  The modem's dump unit -- the "mac-dump" owner bits of
the HP system's SRAM usage register are its one documented trace; how it is
steered follows what the ESP-SDR project found on this chip, rewritten here
-- writes the PHY's ADC output into one 64 KB SRAM bank (bank 3,
0x40830000) the CPU lends it per capture: 10-bit I and Q per word, the
gain index above, at 4 to 80 MS/s.  The bank comes from the SRAM tier, so
the build puts Wi-Fi's packet buffers in PSRAM and its heap at 32 KB.

```
tikuOS:/> sdr start                      bank lent, radio up: "SDR ready"
tikuOS:/> sdr spec 2437 1 256            one spectrum: "SPEC <MHz> <Hz> <gain> <nfft> <hex>"
tikuOS:/> sdr sweep 2404 2484 16 1 128   the band in six slices, then "SWEEP 6"
tikuOS:/> sdr stop
```

Each spectrum is computed on the board -- 32 Hann-windowed blocks, a
fixed-point FFT, the power averaged, half-decibels out -- so a 115200-baud
console carries ten a second; TikuSDR (applications, Device Tools) draws
them.  What the receiver is: the analog filter passes about +-12 MHz at
every rate, so the low rates alias (capture at 40 MS/s or more and
decimate); the dump unit's Q runs opposite to the air, so the samples are
conjugated before the transform (BLE's 2402/2426/2480 land where they
should); there are spurs at 0 and -7 MHz; levels are uncalibrated and the
automatic gain moves them.  It tunes roughly 2.2 to 2.7 GHz.

The transmit side has no counterpart: the dump unit is receive-only and the
PHY's calibration plays tones, never samples.  What was found -- the tone
generators, the transmit test mode, the internal loopback through which the
chip hears its own tone -- is in lab verbs built with
`TIKU_DRV_SDR_ESP_PROBE=1` alone.

## Files

- `tiku_drv_wifi_esp.c/.h` -- the driver descriptor, the XIP check, the
  radio's on/off, joining, the frame path and the `tiku_wireless` interface
- `esp_ble.c`, `tiku_drv_ble_esp.h` -- BLE: the controller's on/off, the
  tables it calls, its configuration, the host's transport
- `esp_npl.c`, `esp_mempool.c`, `esp_ble.h` -- the controller's OS layer
  and memory pools
- `esp_ble_abi.h` -- the controller library's ABI, hand-written
- `esp_core.c` -- what both radios stand on: heap, modem gating, timers,
  the arbiter
- `esp_coex.c`, `esp_coex_abi.h` -- coexistence: the arbiter's adapter, and
  its library's ABI, hand-written
- `esp_sdr.c`, `tiku_drv_sdr_esp.h`, `esp_sdr_xip.ld` -- the receiver:
  the bank's loan, the dump unit, the on-board spectra; and the transmit
  side's lab probes
- `esp_osi.c` -- the OS the libraries run on: locks and queues over kernel
  wait queues, tasks as worker threads, timers on SYSTIMER's driver alarm,
  their interrupt lines on the radio's CLIC lines
- `esp_phy.c` -- the modem's clocks, the PHY's bring-up and calibration,
  the MAC address
- `esp_heap.c/.h` -- the libraries' heap (SRAM, and the PSRAM block
  packet buffers prefer) and lock pool
- `esp_wpa.c` -- the supplicant the stack calls: RSN/WPA element parsing,
  the WPA2-PSK handshakes, the keys to the radio
- `esp_crypto.c` -- the crypto table the libraries call, over TikuKits
- `esp_port.h` -- what those files share
- `esp_abi.h` -- the libraries' ABI this driver uses, hand-written
- `esp_glue.c` -- the symbols the libraries expect around them
- `esp_xip.ld`, `esp_wifi_xip.ld`, `esp_ble_xip.ld`, `esp_coex_xip.ld` --
  the fragments placing the libraries in the XIP window
- `fetch.sh`, `SHA256SUMS` -- download and verification
