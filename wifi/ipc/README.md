# P4 Wi-Fi through the native C6 service

The FireBeetle 2 ESP32-P4 runs the TikuKits IPv4 stack. Its attached C6 runs
station association, an open/WPA2/WPA3 access point and the radio workers. This driver implements
`tiku_wireless_*` through the existing kernel IPC, managed streams, mux and
SDIO mailbox. Applications use the shared Wi-Fi link adapter and network APIs.

## Build profiles

Build the C6 with `MCU=esp32c6 C6_PROFILE=firmware C6_STORAGE=1`, shell and
`TIKU_DRV_WIFI_ESP_ENABLE=1 TIKU_C6_RADIO_SERVICE_ENABLE=1`. The service profile
rejects `TIKU_KIT_NET_ENABLE=1`: the station has one Ethernet consumer on P4.
Install its package with the C6 installer described in the C6 port README.
Update P4 and C6 together when adding AP support: the station-only P4
driver requires an exact capability bitmap and rejects the AP-capable C6.
The AP-capable P4 accepts a station-only C6 and disables AP commands for it.

The P4 firmware profile uses:

```sh
make MCU=esp32p4 BOARD=firebeetle2_esp32p4 P4_PROFILE=firmware \
  TIKU_SHELL_ENABLE=1 TIKU_IPC_ENABLE=1 TIKU_SDIO_C6_ENABLE=1 \
  TIKU_C6_COLD_BOOT_ENABLE=1 TIKU_LPCORE_ENABLE=1 \
  TIKU_DRV_WIFI_IPC_ENABLE=1 TIKU_KIT_NET_ENABLE=1 TIKU_KIT_NET_MIN=1 \
  TIKU_KITS_NET_WIFI_ENABLE=1 TIKU_KITS_NET_DHCP_ENABLE=1 \
  TIKU_KITS_NET_TCP_ENABLE=1
```

The driver requires the FireBeetle board and rejects another wireless
provider in the same image. It sets IP MTU 576, six TCP transmit segments,
two TCP connections with 2048 receive bytes each and eight managed IPC buffers. TCP
starts retransmission after eight service ticks, or one second at 8 Hz. Frame
queues hold four frames per direction on P4 and two on C6. TCP backing and
Wi-Fi staging occupy the high-SRAM IPC carve alongside those queues.
The loader permits up to 64 KiB for this carve and checks the tier and stack boundaries.

## Use

`wifi on` waits for C6 power-up to complete. `wifi scan` and `wifi connect`
queue asynchronous operations; `wifi status` and `wifi list` read the cache.
After association, `wifi up` starts P4 DHCP. `/dev/wifi/ipc0/peer` reports
service availability, build, claim, session and packet queue counters.

The P4 keeps station and AP credentials in RAM for C6-reset recovery.
`wifi save PASSPHRASE` encrypts the active profiles with AES-256-GCM, a fresh
16-byte salt and 12-byte nonce from the P4 entropy source. PBKDF2-HMAC-SHA256
uses 200,000 iterations to derive the key from a 12..128-byte passphrase.
The salt, nonce and format version are authenticated with the ciphertext.
The derived key and plaintext snapshot are erased after the operation.
Neither joining nor starting an AP writes credentials to durable storage.

After a P4 reboot, `wifi unlock PASSPHRASE` authenticates the record and
restores its station/AP intent. Both interfaces must be idle. A wrong
passphrase, altered record or entropy failure returns an error. `wifi saved`
reports whether an encrypted record exists; its SSID remains hidden until
unlock. Wi-Fi commands are excluded from newly recorded shell history.
This format does not import the previous plaintext profile. Saving a new
record does not erase historical flash snapshots or external console logs.
No eFuse provisioning is performed.

`wifi forget` clears the saved record and RAM intent and stops station/AP
operation. Durable-write failures are reported. `wifi off` clears RAM
credentials while retaining the encrypted record. `wifi disconnect` leaves
the station and keeps an active AP in simultaneous mode.

Station authentication supports open, WPA2-PSK and WPA3-SAE.
`wifi connect3 SSID PASSWORD` selects SAE H2E group 19 with required PMF;
`wifi ap start3 SSID PASSWORD [channel]` selects the same AP security.
SAE passwords contain 1 through 63 bytes. Capability discovery refuses these
commands on a peer without SAE/PMF support. Saved profiles retain the selected
authentication mode; recovery does not downgrade it. IEEE 802.15.4 has no
service; Bluetooth uses its separate
[`c6/ble` endpoint](../../ble/ipc/README.md).

## Access point

The AP uses the same exclusive radio claim, credential transfer and Ethernet
mailbox as station mode. Capability discovery gates AP commands. A radio
admits up to four AP clients, each with its own SAE and EAPOL state. Channels
1 through 11 are accepted. A passphrase of 8 through 63 bytes selects WPA2-PSK with
CCMP; an empty passphrase selects an open AP. Station scans and joins require simultaneous mode when the AP is active.

```text
wifi on
wifi ap start TikuP4 test-password 6
wifi ap status
ip
wifi save "example vault passphrase"
wifi ap stop
wifi saved
wifi forget
```

In AP-only mode, P4 runs DHCP at `192.168.4.1/24`, with four 120-second
leases at `192.168.4.2` through `192.168.4.5`. Offers reserve an address for
30 seconds; declined addresses are held for ten seconds. Client identity,
server selection and lease expiry are checked. This mode exposes P4 IP
applications without advertising an Internet gateway.

`wifi ap stop` leaves the radio powered. Each secured peer has independent
nonces, replay counters, retries and pairwise keys. GTK and, for SAE, IGTK
renewal waits for acknowledgements from the remaining clients before
installation. A client that exhausts retries is disconnected without
removing responsive clients. New admissions wait for fresh group keys.
`ESPW_AP_REKEY_SECONDS` selects an interval from 1 through 86400 seconds;
the default is one hour.

## Simultaneous station/AP and forwarding

Select simultaneous mode with both interfaces idle. Wait for each queued
operation to appear in `wifi status` or `wifi ap status` before issuing the
next credential operation:

```text
wifi on
wifi dual on
wifi ap start TikuP4 test-password 6
wifi connect Uplink uplink-password
wifi route on
wifi route status
wifi save "example vault passphrase"
```

The radio uses the station's channel while associated. Adding AP mode to a
joined station restarts the native radio and reconnects the station. Stopping
the AP keeps the station active; leaving the station keeps the AP active.
Station and AP credentials, authentication sessions, MACs and frame identities
are separate. Capability bits advertise simultaneous mode and four-client
admission. The `MODE` operation negotiates interface-tagged frames: increasing
stream object ids use their low bit for STA=0/AP=1, with the Ethernet payload
unchanged. Legacy mode retains its untagged stream and four-byte AP status;
simultaneous mode adds the AP MAC to that status response.

Forwarding is disabled until `wifi route on`. In simultaneous mode, the AP
provides DHCP, ARP, ICMP echo and UDP DNS forwarding; P4 application sockets
use the station address. DHCP advertises `192.168.4.1` as gateway and DNS
server when forwarding is enabled. Clients with an existing lease acquire
those options on renewal. Upstream IPv4 and DNS addresses come from station
DHCP. An overlapping `192.168.4.0/24` uplink is refused by the router.

The router holds 16 endpoint-dependent IPv4 mappings and carries packets up
to 1,500 bytes. It translates TCP/UDP ports, ICMP echo identifiers and inbound
ICMP errors, including path-MTU errors. UDP checksums may be zero on input;
forwarded packets receive calculated IP and transport checksums. TCP mappings
require an outbound SYN, expire after 300 idle seconds, and retain FIN/RST
state for ten seconds. UDP and ICMP mappings expire after 60 idle seconds.
AP source IP/MAC pairs must own a bound DHCP lease. Incoming replies must
match the remote endpoint and an active lease. Local socket APIs reserve
ports 61000..61255 for translation. Exhaustion drops new flows without
evicting active mappings.

IPv4 options, fragmented packets, IPv6, inbound port forwarding, hairpin NAT
and application-specific gateways are outside this profile. DNS forwards
UDP queries to the DHCP-provided resolver; it has no cache or TCP proxy.
Station loss or an uplink-address change removes mappings. The AP DHCP
leases remain while station service recovers. At station lease expiry, the
adapter clears its address and starts a fresh DHCP exchange; it does not
renew the uplink lease in place. A C6 reset clears both links
and restores the requested mode and profiles through the same radio claim.
Forwarding intent is a RAM setting and must be enabled again after P4 reboot.
The bounded frame queues can drop burst traffic; UDP applications supply
retries. `/dev/wifi/ipc0/peer` reports P4 queue counters and `peer_tx_drop` /
`peer_rx_drop` counters from the C6 service.

C6 grants one ten-second claim, renewed every two seconds. Local C6 radio
mutations return busy while claimed. Claim expiry or an IPC session change
cancels packet loans and powers down the station. P4 reacquires a claim and
rejoins its RAM profile after the peer returns. After a working native session
is lost, P4 makes at most three native restart attempts before leaving the
loader available.
An explicit loader or factory request suspends those attempts. A lost link clears staged
packets, ARP entries, DHCP state and TCP connections before DHCP restarts.

WPA2 key derivation runs on a C6 worker so mailbox progress continues. A
cancelled derivation retains its inputs until that worker finishes, then
clears the job and worker stack without starting association.

## Qualification interface

`TIKU_P4_RADIO_TEST=1` adds `/dev/wifi/ipc0/test`. It requires the Wi-Fi IP
profile and TCP. The node runs checked UDP and TCP traffic from P4 process
context. Its write operations are `np <octet-index> <value>`, `nudp`, `ntcp`,
`nserve` and `nstop`; its read operation reports progress and payload errors.
The outbound tests exchange 1000 datagrams of 128 bytes and 1 MiB of TCP data
with echo ports 5000 and 5001. `nserve` listens on TCP port 5003 and echoes up to 512 bytes per step.

The paired runner operates an already installed image and does not flash:

```sh
cd TikuBench
python -m tikubench.core.p4_radio --port /dev/serial/by-id/DEVICE \
  --serial P4_USB_SERIAL --peer-build C6_BUILD \
  --credentials-file /path/to/fixture.json --peer 192.168.77.1 \
  --cycles 100 --output /path/to/new-results-directory
```

The fixture file contains `ssid` and `psk`. The runner suppresses raw console
capture and redacts the key in command results. The fixture must run the
matching echo protocols from `TikuBench/tests/esp32c6/test_wifi.c`.
The runner checks UDP progress and claim continuity during BASIC `DELAY`,
`SLEEP` and `INPUT`. `--basic-waits-only` selects those checks without the
complete UDP and TCP transfers; `--cycles-only` selects power cycling.
Host protocol, ownership, frame and cleanup models run with
`make -C TikuBench/tests/host/kernel ipc-radio wifi-ap wifi-ap-multi wifi-sae-multi wifi-dual wifi-router dhcps-multi net-reserved-ports` from the OS root.

Hardware checks with an ESP32-C5 client cover open, WPA2 and SAE/PMF AP
association, wrong-password refusal, DHCP and checked TCP/UDP traffic.
SAE checks observe GTK and IGTK index rotation during traffic with a
60-second renewal interval. Station checks cover SAE/PMF association,
DHCP and checked UDP traffic against a C5 AP renewing keys every 60 seconds.
Recovery checks cover C6 reset, stale-PMKID rejection followed by SAE retry,
IP removal on power-off and saved-profile removal across reboot.

Extension host tests cover independent AP admission and group-key acknowledgement,
SAE role isolation, four DHCP leases, interface-tagged mailbox frames, NAT/DNS
translation and encrypted dual-profile restoration. Guard-removal controls run
with `python3 TikuBench/tests/host/kernel/test_wifi_extensions_mutations.py`.
Paired FireBeetle 2 and C5 checks cover simultaneous WPA2 and SAE/PMF
station/AP links, DHCP, UDP/TCP forwarding and DNS relay. The C5 runs the
uplink AP and a downstream station; test sockets bind to the downstream
interface and pass through the P4 translator. Checks cover station departure
with the AP retained, AP shutdown with the station retained, encrypted save
during traffic, locked P4 reboot, wrong-passphrase rejection and restoration
of both profiles after unlock. A C6 reset restores both SAE roles and routed
traffic; stale client PMKIDs require a fresh SAE exchange.

Four-client admission, key acknowledgement and lease ownership are covered
by host models. Concurrent traffic from four physical clients, AP/BLE
coexistence and physical power-cycle restoration are not qualified.
