# FireBeetle P4 Bluetooth over C6 IPC

The P4 runs the existing Bluetooth host in `tikukits/net/bluetooth`.
Its `tiku_bt_transport_t` carries HCI commands, events and ACL packets through
`c6/ble`, a kernel mailbox endpoint on channel 3 of the P4/C6 SDIO multiplexer.
The native C6 controller owns radio timing. Application code uses
`interfaces/bluetooth/tiku_bt.h`, the BLE serial/advertising facades or the
existing shell and BASIC commands.

## Paired build

Fetch the native radio assets with
`sh drivers/wifi/esp/c6/fetch.sh --ble`. Build C6:

```sh
make MCU=esp32c6 C6_PROFILE=firmware C6_STORAGE=1 \
    TIKU_SHELL_ENABLE=1 TIKU_SHELL_BASIC_ENABLE=1 \
    TIKU_DRV_WIFI_ESP_ENABLE=1 TIKU_DRV_BLE_ESP_ENABLE=1 \
    TIKU_C6_RADIO_SERVICE_ENABLE=1 TIKU_C6_BLE_SERVICE_ENABLE=1 \
    C6_BUILD=42 firmware-image \
    C6_RECOVERY_IMAGE=/path/to/tiku_c6_access.bin
```

Build P4:

```sh
make MCU=esp32p4 BOARD=firebeetle2_esp32p4 TIKU_SHELL_ENABLE=1 \
    TIKU_IPC_ENABLE=1 TIKU_SDIO_C6_ENABLE=1 TIKU_C6_COLD_BOOT_ENABLE=1 \
    TIKU_DRV_WIFI_IPC_ENABLE=1 TIKU_DRV_BLE_IPC_ENABLE=1 \
    TIKU_KIT_NET_ENABLE=1 TIKU_KIT_NET_MIN=1 \
    TIKU_KITS_NET_WIFI_ENABLE=1 TIKU_KITS_NET_DHCP_ENABLE=1 \
    TIKU_KITS_NET_TCP_ENABLE=1
```

Use the existing checked P4 paired-image flasher and C6 installer described
in the port READMEs. The profile requires the radio-service multiplexer and
refuses a second Bluetooth controller provider. A peer without `c6/ble`
does not become ready for Bluetooth.

## Ownership and recovery

`bt on` discovers capabilities, claims the controller and initializes the
P4 host. `bt off` resets the host and releases the controller. Discovery
and queue counters appear at `/dev/ble/ble-ipc0/peer`. `bt scan`,
`bt advertise <name>`, `bt connect` and `bt connections` use the P4 host.

One claim lasts ten seconds and is renewed every two seconds. The claim is
bound to the negotiated mailbox session. A native C6 host already using the
controller refuses a P4 claim. While P4 owns it, C6 application HCI and power
calls are refused. Identical control retries replay the previous response.
The two hosts keep separate bond stores. A bonded peer must forget and pair
again when controller ownership moves to a host without its key.

Session loss, lease expiry, a failed managed packet transfer or a dropped
controller packet invalidates the claim and stops admission. The P4 forgets
connections and advertising state. Use `bt on` after peer recovery; a lost
controller session is not restored by replaying old HCI traffic.

Each direction has two complete-packet slots. Managed buffers carry a
four-byte claim followed by one H4 packet, at most 260 bytes. The wire layer
handles fragmentation, acknowledgments and retries. Full queues apply
backpressure. Buffer backing remains owned until completion or cancellation.
Wi-Fi, BLE and bulk streams use distinct logical endpoints on the same
physical SDIO link.

The controller profile supports one LE connection and one advertising set.
The shared host supports ATT MTU up to 247 and 1M, 2M and Coded connection
PHY requests. `bt phy coded`, `bt phy coded-s2` and `bt phy coded-s8` select
Coded with no coding preference, S=2, or S=8 for the local transmitter.
`tiku_bt_set_phy()` accepts `TIKU_BT_PHY_CODED`;
`tiku_bt_set_coded_phy()` also accepts the transmit coding preference.
The controller may override that preference, and the peer chooses its own
transmit coding. `bt connections` reports the negotiated PHY as `Coded`;
HCI PHY completion events do not distinguish S=2 from S=8.
Discovery supports legacy PDUs and extended advertising with 1M or Coded
primary PHYs; secondary advertising supports 1M, 2M or Coded. Coded links
start through Coded discovery and retain that PHY after connection.

```text
bt on
bt advertise-ext TikuP4 coded
bt advertise stop
bt scan-ext both Coded
bt list
bt scan stop
bt connect 1
bt connections
```

`bt advertise-ext <name> 1m|coded [1m|2m|coded]` selects the primary and
secondary PHYs; the secondary defaults to the primary. `bt scan-ext`
accepts `1m`, `coded` or `both` and an optional name prefix. `bt list`
includes both PHYs and the advertising SID. Connecting to a scan entry
uses its primary PHY. An explicit address accepts
`bt connect <address> public|random [1m|coded]`.

Applications use `tiku_bt_advertise_extended()`,
`tiku_bt_advertise_extended_raw()`, `tiku_bt_scan_extended()` and
`tiku_bt_connect_phy()`. Extended payloads are bounded at 251 bytes.
The controller rejects connectable data that exceeds one advertising PDU.
The host reassembles two concurrent reports; truncated and oversized
reports are discarded, and assembly exhaustion stops scanning. Cache
entries distinguish address type and SID. The legacy advertising hook
receives legacy PDUs; extended reports populate the scan cache.

The P4 and native C6 enable `TIKU_BT_EXTENDED`. Controller feature bits gate
Coded and extended operations. On an extended-capable controller, legacy
APIs also use extended HCI commands, with legacy advertising PDUs, so the
application can switch formats without resetting Bluetooth. The HCI traffic
uses the existing mailbox transport and ownership rules.

The host requests LE Secure Connections pairing on a peripheral connection.
A Linux test peer needs a BlueZ pairing agent to accept that request; a GATT
client without one can exchange data before the pairing timeout disconnects it.
The mailbox CRC and claim generation do not authenticate an external peer.

## Checks

```sh
make -C TikuBench/tests/host/kernel c6-ble c6-radio ipc-radio
```

The tests exercise controller ABI layouts, shared RF and clock ownership,
HCI packet bounds, queue backpressure, duplicate controls, lease expiry,
session fencing and host invalidation. Mutation checks remove those guards
and require the corresponding test to fail. Hardware captures remain in
`kintsugi/` or `temp/`.

The installed-pair runner powers Wi-Fi off, cycles the controller and checks
that the C6 returns its worker slots without changing the mailbox session.
`--ownership` also tests refusal in both host ownership directions:

```sh
cd TikuBench
python -m tikubench.core.p4_ble --port /dev/serial/by-id/DEVICE \
  --serial P4_USB_SERIAL --peer-mac C6_MAC --peer-build C6_BUILD \
  --cycles 100 --ownership --output /path/to/new-results-directory
```

The runner does not flash. It leaves BLE off and writes its result and console
capture under the supplied output directory.
