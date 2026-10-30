# P4 Wi-Fi through the native C6 service

The FireBeetle 2 ESP32-P4 runs the TikuKits IPv4 stack. Its attached C6 runs
station association, WPA2 and the radio workers. This driver implements
`tiku_wireless_*` through the existing kernel IPC, managed streams, mux and
SDIO mailbox. Applications use the shared Wi-Fi link adapter and network APIs.

## Build profiles

Build the C6 with `MCU=esp32c6 C6_PROFILE=firmware C6_STORAGE=1`, shell and
`TIKU_DRV_WIFI_ESP_ENABLE=1 TIKU_C6_RADIO_SERVICE_ENABLE=1`. The service profile
rejects `TIKU_KIT_NET_ENABLE=1`: the station has one Ethernet consumer on P4.
Install its package with the C6 installer described in the C6 port README.

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
The loader permits up to 48 KiB for this carve and checks the tier and stack boundaries.

## Use

`wifi on` waits for C6 power-up to complete. `wifi scan` and `wifi connect`
queue asynchronous operations; `wifi status` and `wifi list` read the cache.
After association, `wifi up` starts P4 DHCP. `/dev/wifi/ipc0/peer` reports
service availability, build, claim, session and packet queue counters.

The P4 keeps the requested SSID and passphrase in RAM for C6-reset recovery.
`wifi forget`, `wifi off` and P4 reset remove that profile. An unavailable C6
makes power-off return an error, while the P4 still clears its reconnect
intent. Persistent credentials are not advertised. The native C6 service
supports open and WPA2-PSK station mode. This driver does not expose SoftAP,
BLE or IEEE 802.15.4 operations.

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
`make -C TikuBench/tests/host/kernel ipc-radio` from the OS root.
