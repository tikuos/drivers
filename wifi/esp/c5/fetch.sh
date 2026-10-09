#!/bin/sh
# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# fetch.sh - fetch pinned ESP32-C5 PHY assets and verify their SHA-256 hashes.
# SPDX-License-Identifier: Apache-2.0
set -eu
case "${1-}" in ""|--wifi|--all) ;; *) echo "usage: $0 [--wifi|--all]" >&2; exit 2;; esac
cd "$(dirname "$0")"
PHY=https://raw.githubusercontent.com/espressif/esp-phy-lib/20f1db053a0e6cb9f1c09d255c43bf42483041d0
ROM=https://raw.githubusercontent.com/espressif/esp-idf/4d59230ddff16327812782151ef0afef202dc6d7/components/esp_rom/esp32c5/ld
mkdir -p vendor
curl --fail --location --max-time 60 -o vendor/libphy.a "$PHY/esp32c5/libphy.a"
curl --fail --location --max-time 60 -o vendor/LICENSE-phy "$PHY/LICENSE"
for file in esp32c5.rom.ld esp32c5.rom.phy.ld esp32c5.rom.version.ld; do
    curl --fail --location --max-time 60 -o "vendor/$file" "$ROM/$file"
done
if command -v sha256sum > /dev/null; then
    sha256sum -c SHA256SUMS
else
    shasum -a 256 -c SHA256SUMS
fi
if [ "${1-}" = --wifi ] || [ "${1-}" = --all ]; then
    WIFI=https://raw.githubusercontent.com/espressif/esp32-wifi-lib/af55a0ca258ce9d791d1661d7c2bbb65f08c0c21
    for file in libnet80211.a libpp.a libcore.a; do
        curl --fail --location --max-time 60 -o "vendor/$file" "$WIFI/esp32c5/$file"
    done
    curl --fail --location --max-time 60 -o vendor/LICENSE-wifi "$WIFI/LICENSE"
    for suffix in api coexist net80211 pp; do
        curl --fail --location --max-time 60 -o "vendor/esp32c5.rom.$suffix.ld" "$ROM/esp32c5.rom.$suffix.ld"
    done
    if command -v sha256sum > /dev/null; then
        sha256sum -c SHA256SUMS-wifi
    else
        shasum -a 256 -c SHA256SUMS-wifi
    fi
fi
