# CYW43439 WiFi driver

Clean-room WiFi driver for the Infineon CYW43439 (Pico 2 W radio).
Apache-2.0 licensed; the chip's internal firmware blob is the
only non-Apache piece and lives in `firmware/` with its own
licence.

## Status

| Phase | Deliverable | State |
|-------|-------------|-------|
| 0     | Registry shell, boot message | ✓ verified on Pico 2 W |
| 1     | gSPI transport — TEST_RO/TEST_RW pass | ✓ done 2026-05-12 |
| 2.A   | Chip wake + 32-bit mode configure + ALP clock | ✓ done 2026-05-12 |
| 2.B   | F1 backplane chipcommon ChipID read = 0xA9AF | ✓ done 2026-05-12 |
| 2.C   | Disable WLAN/ARM cores via backplane core control | ✓ done 2026-05-12 |
| 2.D   | Block read/write infrastructure (read_block / write_block) | ✓ done 2026-05-12 |
| 2.E   | Firmware blob embedding (43439A0.bin = 231 KB; CLM = 984 B) | ✓ done 2026-05-12 |
| 2.F   | Firmware upload + chip cores out of reset (HT clock UP) | ✓ done 2026-05-12 |
| 3.A   | F2 bus enable (watermark + IRQ enable → F2_RX_READY=1) | ✓ done 2026-05-12 |
| 3.B   | SDPCM RX primitive (parse first chip-sent event frame) | ✓ done 2026-05-12 |
| 3.C   | CDC IOCTL request/response — `GET("ver")` returns fw fingerprint | ✓ done 2026-05-12 |
| 3.D   | CLM blob upload via clmload IOCTL (clmload_status = 0)   | ✓ done 2026-05-12 |
| 3.E   | Radio config IOCTLs + MAC = 2c:cf:67:b0:f2:7f             | ✓ done 2026-05-12 |
| 3.F   | Event channel reader — RADIO + PROBREQ_MSG events parsed | ✓ done 2026-05-12 |
| 4.A   | Active scan — over-air discovery of real APs (BSSID/SSID/RSSI/channel) | ✓ done 2026-05-12 |
| 4.B   | WPA2 join (set ssid/pmk, listen for WLC_E_LINK) | pending (needs credentials to verify) |
| 5     | Ethernet → IP link adapter   | pending |
| 6     | VFS nodes + shell command    | pending |

## Acceptance criterion for phase 1 completion

`[cyw43] probe: gSPI bus alive (TEST_RO=0xfeedbead)` on the UART.
Until that line appears, the bus isn't talking; everything after
that point in the phase plan depends on it.

## Build

```
make MCU=rp2350 TIKU_DRV_WIFI_CYW43_ENABLE=1
```

## Spec sources

- Infineon `wifi-host-driver` repo (protocol headers, IOCTL IDs,
  event codes). Authoritative.
- `cyw43-driver` source — readable as cross-reference *only*. Do
  not copy code; the project's licence is non-commercial / RP-only.
- CYW43 datasheet (limited public info; mostly via Pi-foundation
  derived docs).

## Hardware pinout (Pico 2 W, fixed by board design)

| Signal       | GP  | Direction       |
|--------------|-----|-----------------|
| WL_REG_ON    | 23  | Out (power on)  |
| WL_DATA      | 24  | Bidirectional   |
| WL_CS        | 25  | Out             |
| WL_CLOCK     | 29  | Out             |

GP29 is shared with VSYS-divide ADC channel 3. While the WiFi is
active the battery-voltage ADC read is unavailable.

Pin defines come from `arch/arm-rp2350/boards/tiku_board_rpi_pico2_w.h`
(`TIKU_BOARD_CYW43_*_PIN`).

## Firmware blob

The chip's internal Cortex-M3 needs ~225 KB of Infineon firmware
uploaded after every reset. Place the binary at
`firmware/43439A0.bin` with the redistribution licence file at
`firmware/LICENSE.infineon` alongside.

Source: <https://github.com/Infineon/wifi-host-driver/tree/master/resources/firmware/COMPONENT_43439>

The firmware is permissively redistributable for use with the
CYW43439 chip; the LICENSE file in the same directory carries
the full terms.
