#!/bin/sh
# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# fetch.sh - Fetch and verify pinned ESP32-C6 Wi-Fi, PHY and ROM assets.
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")"
BLE=https://raw.githubusercontent.com/espressif/esp32c6-bt-lib/a00f2d0a19c54b9ca31358754fbb8b9a08cbb92c
WIFI=https://raw.githubusercontent.com/espressif/esp32-wifi-lib/af55a0ca258ce9d791d1661d7c2bbb65f08c0c21
PHY=https://raw.githubusercontent.com/espressif/esp-phy-lib/20f1db053a0e6cb9f1c09d255c43bf42483041d0
COEX=https://raw.githubusercontent.com/espressif/esp-coex-lib/c758e7b56e0fa22177a0539796e1df59978dc322
IDF=https://raw.githubusercontent.com/espressif/esp-idf/4d59230ddff16327812782151ef0afef202dc6d7
mkdir -p vendor
for file in libnet80211.a libpp.a libcore.a; do
    curl --fail --location --max-time 60 -o "vendor/$file" "$WIFI/esp32c6/$file"
done
for file in libphy.a libbtbb.a; do
    curl --fail --location --max-time 60 -o "vendor/$file" "$PHY/esp32c6/$file"
done
curl --fail --location --max-time 60 -o vendor/libcoexist.a "$COEX/esp32c6/libcoexist.a"
curl --fail --location --max-time 60 -o vendor/LICENSE-wifi "$WIFI/LICENSE"
curl --fail --location --max-time 60 -o vendor/LICENSE-phy "$PHY/LICENSE"
curl --fail --location --max-time 60 -o vendor/LICENSE-coex "$COEX/LICENSE"
curl --fail --location --max-time 60 -o vendor/LICENSE-idf "$IDF/LICENSE"
for suffix in '' .api .coexist .net80211 .pp .phy .version; do
    file="esp32c6.rom$suffix.ld"
    curl --fail --location --max-time 60 -o "vendor/$file" \
        "$IDF/components/esp_rom/esp32c6/ld/$file"
done
if [ "${1:-}" = --ble ]; then
    curl --fail --location --max-time 60 -o vendor/libble_app.a "$BLE/esp32c6/libble_app.a"
    curl --fail --location --max-time 60 -o vendor/NOTICE-ble "$BLE/NOTICE"
    if command -v sha256sum >/dev/null; then
        sha256sum -c SHA256SUMS-ble
    else
        shasum -a 256 -c SHA256SUMS-ble
    fi
fi
if command -v sha256sum >/dev/null; then
    sha256sum -c SHA256SUMS
else
    shasum -a 256 -c SHA256SUMS
fi
