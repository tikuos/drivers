# ESP32-S31 radio adapters

The S31 Wi-Fi backend implements the shared `tiku_wireless_*` interface
through Espressif's MAC/PHY libraries, TikuOS workers and the shared WPA
adapter. Scanning, WPA2 association, DHCP and bidirectional UDP have hardware
evidence across three start/stop cycles. It runs on CPU0 with a 40 MHz crystal. Library instructions use
flash; interrupt paths, descriptors and the 64 KiB radio heap use SRAM.

## Assets and ABI

From the TikuOS root:

```sh
python3 drivers/wifi/esp/s31/fetch.py
python3 drivers/wifi/esp/s31/check_abi.py --idf /path/to/esp-idf \
    --cc riscv32-esp-elf-gcc --output /tmp/s31-radio-abi.json
```

`assets.json` pins downloads and SHA-256 digests. `SHA256SUMS` is checked by
the radio build profiles. `vendor/` is ignored. The fetcher includes the
Wi-Fi, PHY and coexistence archive licenses and the S31 ROM address maps.
The ABI checker compares compiler-produced sizes and offsets against the
SDK headers, including the S31-specific 512-byte Wi-Fi OSI table, station
configuration/events, BTDM callback declarations, fixed-block pool layouts
and the 80-byte coexistence adapter.
The pinned SDK is
`4d59230ddff16327812782151ef0afef202dc6d7`.

Bluetooth controller archives are excluded from the fetcher. The optional
BLE build takes externally supplied archives through `S31_BT_LIB_DIR`.
The audited `esp32s31-bt-lib` revision
`10c507788e9da0993709cf82e405c896561172d8` contains NimBLE attribution in
`README.md` and `NOTICE`; the audit has not established redistribution
terms covering the complete controller archives. Redistribution requires
resolving those terms.

## Ownership

`esp_core.c` manages Wi-Fi and optional BLE ownership. The default build
requires one radio to stop before the other starts. Start acquires the PHY,
hardware entropy source and Wi-Fi DMA master 3's SRAM access. A locked,
untrusted DMA master is refused. Stop drains the radio workers and restores
the acquired DMA permission and digital clock fields before releasing the heap.

`S31_RADIO_COEX=1 S31_BLE_ENABLE=1` selects the shared-radio adapter and an
88 KiB SRAM heap. The OS timer service starts before coexistence registration;
PHY calibration requires the registered ROM adapter. Initialization failure
refuses startup. Each radio retains ownership through its controller shutdown,
and the final release restores the shared clocks and DMA permissions. The BLE
startup path preserves security engines held by Wi-Fi. This configuration
does not use PSRAM for radio packet buffers.

The PHY calibrates once after reset and uses its wakeup entry on later
starts. The vendor calibration call has no software timeout. Shared BB/APB
clocks remain enabled until PHY shutdown completes. Station modem sleep is explicitly disabled. Analog and BBPLL power
are retained until reset; this backend does not implement modem sleep or
qualify radio energy consumption. A hardware entropy health fault disables
further random output until reset. Radio callbacks requiring randomness
terminate firmware when that source is unavailable. RF-load testing has
observed this failure; sustained radio reliability remains unqualified.

`radio_memory.c` supplies `memcpy` to the radio profiles. Aligned word copies
use 32-bit loads and stores, including 16-byte copies. The vendor MAC library
uses this function to write its hardware key table, whose entries require
word accesses. The data fixture verifies installed keys by word readback and
reports match counts without retaining key material.

CPU0 peripheral interrupt slots 20..27 serve Wi-Fi and its timer. One
logical Wi-Fi interrupt can dispatch several physical sources. BTDM has
slots 28..31. Source claims are exclusive and releases restore the saved
interrupt-matrix routing. The UART, kernel tick and hardware-timer sources
cannot be claimed by radio callbacks.

## Qualification profiles

`make MCU=esp32s31 S31_PROFILE=radio-os-test` builds the OS adapter fixture
without radio archives. It exercises queues, worker cancellation, NPL
pools, BTDM packet pools, callout timing, hardware randomness and both Wi-Fi and BTDM software
interrupt routes across three start/stop cycles. Run the RAM tool with
`--radio-os-test --seconds 8`.

`S31_PROFILE=phy-test` exercises calibration, wakeup, shutdown, clock
restoration and an injected entropy-source failure. Run the RAM tool with
`--phy-test --seconds 8`. Random-source fault detection is not entropy
certification.

`S31_PROFILE=flash-wifi-test` runs three public-driver scan/start/stop
cycles. `S31_WIFI_COUNTRY` selects the regulatory country code; its default
is `01`. Use the installed location's country code. The flash tool accepts
this profile with `--radio-test`; successful qualification requires
received AP records, radio interrupts and ownership/heap restoration.
The guarded flash tool requires the original factory backup and its hash,
verifies the test image and restores the overwritten factory span.

An optional `S31_WIFI_TEST_HEADER=/absolute/private/network.h` enables
association, DHCP and sixteen sequenced UDP echoes per lifecycle. Define
`S31_TEST_SSID`, `S31_TEST_PSK`, `S31_TEST_PEER` (four IPv4 bytes in braces)
and `S31_TEST_PORT` in that private header. The peer must echo each received
128-byte UDP payload. The profile uses a 1500-byte IP MTU. Select an output
directory outside the repository with `S31_DIR=/absolute/private/build`;
these artifacts contain credentials and must be removed after the test.
Use `--radio-test --wifi-data-required --seconds 150` to require the data
checks in the flash runner's result. Keep credentials and private images
outside captures, source control and shared build directories.

## Normal firmware and credentials

`S31_PROFILE=firmware` starts the native driver and Wi-Fi IP service behind
the UART shell. The radio starts powered off. Station association selects
DHCP client operation; AP activation selects the DHCP server at 192.168.4.1.
The shell supports `wifi`, `ip` and `ping`. The graphical display profile is
a separate build and has no network settings screen.

Station and AP modes share the WPA2 authenticator, SAE Hash-to-Element
implementation and PMF handling with the ESP radio driver. `wifi connect3`
and `wifi ap start3` select SAE and required PMF. This profile switches
between station and AP; it exposes no simultaneous station/AP or routing
control. AP channels are 1..11. `S31_WIFI_COUNTRY` applies on each radio
start and defaults to `01`; build with the installed location's code.

```text
wifi on
wifi scan
wifi list
wifi connect SSID PASSWORD
ip
ping 192.168.1.1 4
wifi disconnect
wifi ap start ACCESS_POINT PASSWORD 6
wifi up
wifi ap status
wifi ap stop
wifi ap start3 ACCESS_POINT PASSWORD 6
wifi ap status
wifi off
```

`wifi save PASSPHRASE` saves the active station or AP profile, powers the
radio off and encrypts credentials with AES-256-GCM. The vault key uses
PBKDF2-HMAC-SHA256 with 200,000 rounds, a fresh 16-byte random salt and a
12-byte random nonce. The passphrase must contain 12..128 bytes. Key
derivation runs in the shell foreground and services other processes and
the watchdog. Radio mutations are refused while a profile operation owns
that state. The shell echoes input; keep credential-bearing console captures
private. The SSID and wireless password stay in RAM while in use.

The vault occupies the final two 4 KiB sectors of the 16 MiB flash. Access
requires a readable partition table whose partitions end before 0xffe000.
The backend refuses damaged or foreign records. A write commits its CRC-
checked record before erasing the predecessor; a torn replacement leaves
the previous or new record readable. An interrupted first write can leave
uninitialized damaged storage requiring external restoration. These
sectors are reserved even though the factory partition table has no entry
for them. Flash operations require kernel foreground context and use SRAM
buffers with caches and interrupts suspended during each ROM transaction.

After reboot, `wifi saved` reports a locked profile. `wifi unlock PASSPHRASE`
authenticates the envelope before starting the stored mode; it requires the
radio to be off. A wrong passphrase or modified envelope is refused before
radio startup. `wifi forget` powers the radio off, commits an empty record
and erases the previous record. The backend stores one mode at a time and
does not implement automatic unlock, flash rollback protection or secure
boot. It does not expose a general persistent VFS medium.

Normal-firmware station association, DHCP and ICMP replies, WPA2/WPA3 AP
client data and encrypted-profile save/reboot/unlock/forget have hardware
qualification. An independent C5 peer authenticated to both AP modes and
received 300/300 replies to 512-byte pings while the AP rotated its group
key at a test interval of ten seconds. Wrong passwords were rejected. The
station installed renewed group keys from the C5 AP in both modes and
received eight replies before and after renewal. WPA3 uses SAE and required
PMF; this check does not qualify forged-management-frame handling.

Station recovery makes three automatic rejoin attempts. After exhausting
that budget, use `wifi connect` or `wifi connect3` when the AP returns.
Station shutdown releases the vendor's copy of the WPA3 RSNXE before
releasing the shared heap. Modem sleep, graphical network settings and
concurrent audio/display/storage traffic remain outside this profile.

## BLE controller and host

`S31_BLE_ENABLE=1` adds the native BTDM controller, bounded HCI transport,
TikuKits BLE host and `bt` commands to the normal firmware profile. The
default controller build and Wi-Fi share exclusive PHY ownership. Select
`S31_RADIO_COEX=1` to enable concurrent ownership as described above. Modem
sleep is disabled while BLE is on.

The build requires an external ESP-IDF source tree at
`4d59230ddff16327812782151ef0afef202dc6d7` and the `libble_app.a` and
`libbtdm_common.a` archives from `esp32s31-bt-lib` revision
`10c507788e9da0993709cf82e405c896561172d8`. `ble_assets.json` records the
required input hashes; the build refuses missing or changed inputs.
The Bluetooth controller archives are not distributed here. Their
redistribution terms remain unresolved in the port's asset audit.

```sh
make MCU=esp32s31 S31_PROFILE=firmware S31_BLE_ENABLE=1 \
    S31_BT_IDF=/path/to/esp-idf S31_BT_LIB_DIR=/path/to/esp32s31-bt-lib
```

```text
wifi off
bt on
bt scan
bt list
bt scan stop
bt connect 1
bt conns
bt discover
bt phy 2m
bt phy coded-s8
bt pair
bt disconnect
bt advertise-ext TikuS31 coded coded
bt advertise stop
bt off
```

The host supports central and peripheral roles, legacy and extended
advertising/scanning, 1M/2M/Coded PHYs, GATT client/server operations and
LE Secure Connections. It uses the S31 health-checked random source and
software P-256 when the controller lacks that HCI operation. ECDH yields to
native controller workers after each four scalar bits. The progress callback
does not dispatch host packets or reenter P-256. The profile
budgets two connections and two advertising sets. C5 qualifies each
connection role. C5 and a Linux Bluetooth peer each receive and check
128 KiB of notifications on two simultaneous encrypted links, using 2M and
1M respectively. C5 independently discovers two advertising sets on 1M
and Coded PHYs.

The controller uses a 56 KiB SRAM heap and a 4096-byte receive ring. HCI
lengths are checked before allocation. Buffer exhaustion returns an error;
receive overflow latches a transport fault cleared by `bt off` followed by
`bt on`. A failed controller shutdown retains radio ownership; another
`bt off` retries teardown. The host scratch arena has fixed placement so radio heap release
does not leave it between free SRAM ranges. The linker places foreground
cryptography, BLE host code, formatted output and software double-precision
arithmetic in flash while retaining vendor IRAM sections in SRAM. The firmware mapping has room for 1 MiB of mapped code.

`btdm_s31_osal.c` supplies native workers, interrupts and NPL callbacks.
`btdm_s31_mempool.c` binds packet pools and selective controller-pool teardown
to the shared allocator. The BLE adapter reserves additional bounded NPL
objects used outside the SDK's reported counts. Controller configuration
size and field offsets are checked at compile time.

C5 peer tests cover discovery, connections in both roles, GATT reads and
writes, 2M/Coded PHY transitions, Coded discovery and encrypted pairing.
The negotiated limits are a 247-byte ATT MTU and 251-byte link-layer packets.
The C5 received and checked 4096 notification bytes over a Coded connection.
Bond reuse and encrypted data transfer pass after software rebooting both
boards. Encryption-key refresh on one link permits pairing the second peer.

Concurrent Wi-Fi/BLE qualification starts the radios in both orders. Each
order transfers 256 KiB of checked BLE notifications, with 197/200 replies
to simultaneous 512-byte Wi-Fi pings and no heap allocation refusals.
Wi-Fi remains usable after BLE shutdown, and GATT reads work after Wi-Fi
shutdown. These results do not qualify lossless Wi-Fi delivery under BLE
traffic. Long-duration operation, physical power removal, sleep current,
multiple AP clients and combined audio/display/storage traffic remain
outside this qualification.
