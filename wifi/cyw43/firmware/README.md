# CYW43439 firmware blobs (not in git — download once)

```sh
cd drivers/wifi/cyw43/firmware
BASE=https://raw.githubusercontent.com/embassy-rs/embassy/main/cyw43-firmware
curl -LO $BASE/43439A0.bin
curl -LO $BASE/43439A0_clm.bin
curl -L  -o nvram.bin $BASE/nvram_rp2040.bin
curl -LO $BASE/43439A0_btfw.bin   # only for TIKU_DRV_WIFI_CYW43_BT_ENABLE=1
```

Verify:

```sh
shasum -a 256 -c <<'EOF'
5555e0261da2610a500d68c18d895cace0152bbefbf76f4aa683ebce77e3d7eb  43439A0.bin
e712b3d218e8b1e2747b092e03b8b0afcb8c8c8e355d2a4a0d47b493800f3f89  43439A0_clm.bin
4904bdbb0c937bd0ac2eb2a1d62f2da4dd90e32082384e02874e8d671b0f330d  nvram.bin
ce1992c1a6a16ae51bc012439486e9fb212623eca92d9e82a8090c2acf7ef1df  43439A0_btfw.bin
EOF
```

These are Infineon's chip firmware (WLAN v7.95.61) + Broadcom board NVRAM,
uploaded into the radio at every boot. Licence: **Infineon Permissive Binary
License 1.0** ([full text](LICENSE-permissive-binary-license-1.0.txt)) —
unmodified binary redistribution permitted, but not OSI open source, hence
not tracked here. Same binaries as pico-sdk/MicroPython/Zephyr; upstream
chain: Infineon → georgerobotics/cyw43-driver → embassy-rs.
