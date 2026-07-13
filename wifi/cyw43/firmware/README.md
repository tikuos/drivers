# CYW43439 firmware blobs — download & licence

The CYW43439 radio (Pico W / Pico 2 W) has no non-volatile storage: the
driver uploads Infineon's chip firmware into the radio's own Cortex-M3
after every reset. Those blobs are **binary-only Infineon/Cypress
software, not tikuOS code**, so they are deliberately **not tracked in
this repository** — you download them once into this directory and the
build embeds them (`firmware.S` `.incbin`).

## Download

From the embassy-rs firmware bundle (raw copies of Infineon's official
binaries, the same ones shipped by pico-sdk, MicroPython and Zephyr):

```sh
cd drivers/wifi/cyw43/firmware
BASE=https://raw.githubusercontent.com/embassy-rs/embassy/main/cyw43-firmware
curl -LO $BASE/43439A0.bin
curl -LO $BASE/43439A0_clm.bin
curl -LO $BASE/43439A0_btfw.bin
curl -L  -o nvram.bin $BASE/nvram_rp2040.bin
```

Verify (SHA-256 of the versions this driver was validated against —
WLAN firmware v7.95.61, 2023-01-11):

```sh
shasum -a 256 -c <<'EOF'
5555e0261da2610a500d68c18d895cace0152bbefbf76f4aa683ebce77e3d7eb  43439A0.bin
e712b3d218e8b1e2747b092e03b8b0afcb8c8c8e355d2a4a0d47b493800f3f89  43439A0_clm.bin
ce1992c1a6a16ae51bc012439486e9fb212623eca92d9e82a8090c2acf7ef1df  43439A0_btfw.bin
4904bdbb0c937bd0ac2eb2a1d62f2da4dd90e32082384e02874e8d671b0f330d  nvram.bin
EOF
```

| file               | what                                     | copyright        |
|--------------------|------------------------------------------|------------------|
| `43439A0.bin`      | WLAN firmware v7.95.61                    | Infineon/Cypress |
| `43439A0_clm.bin`  | Country Locale Matrix (regulatory data)   | Infineon/Cypress |
| `43439A0_btfw.bin` | Bluetooth firmware                        | Infineon/Cypress |
| `nvram.bin`        | Pico W/2 W board NVRAM (cyw943439wlpth)   | Broadcom         |

Upstream chain: Infineon → `georgerobotics/cyw43-driver` →
`embassy-rs/embassy` `cyw43-firmware/` (raw `.bin` form used here).

## Licence

Infineon licenses these binaries under the **Permissive Binary License,
Version 1.0** — full text in
[`LICENSE-permissive-binary-license-1.0.txt`](LICENSE-permissive-binary-license-1.0.txt)
(kept in git; byte-identical to the copy in Infineon's
`wifi-host-driver` repository). In short: redistribution **in binary
form, without modification** is permitted, royalty-free, commercial use
included, provided the copyright notice/licence/disclaimer accompany any
redistribution, the blobs are not reverse engineered, and no endorsement
by Infineon is implied. The PBL is not an OSI-approved open-source
licence, which is why tikuOS keeps the blobs out of its repositories and
ships this pointer instead — everything tracked in git stays Apache-2.0.
