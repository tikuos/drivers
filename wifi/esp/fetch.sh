#!/bin/sh
# drivers/wifi/esp/fetch.sh -- download Espressif's ESP32-C61 radio libraries
# (Wi-Fi, PHY, the BLE controller and baseband) and ROM symbol scripts into
# vendor/, at the commits ESP-IDF 4d59230 pins, then check every file
# against SHA256SUMS.  Apache-2.0, not tracked in git.
set -eu
cd "$(dirname "$0")"
WIFI=https://raw.githubusercontent.com/espressif/esp32-wifi-lib/af55a0ca258ce9d791d1661d7c2bbb65f08c0c21/esp32c61
PHY=https://raw.githubusercontent.com/espressif/esp-phy-lib/20f1db053a0e6cb9f1c09d255c43bf42483041d0/esp32c61
BLE=https://raw.githubusercontent.com/espressif/esp32c6-bt-lib/a00f2d0a19c54b9ca31358754fbb8b9a08cbb92c/esp32c61
ROM=https://raw.githubusercontent.com/espressif/esp-idf/4d59230ddff16327812782151ef0afef202dc6d7/components/esp_rom/esp32c61/ld
mkdir -p vendor/lib vendor/rom
for f in libnet80211.a libpp.a libcore.a; do
    curl -sfL -o "vendor/lib/$f" "$WIFI/$f"
done
for f in libphy.a libbtbb.a; do
    curl -sfL -o "vendor/lib/$f" "$PHY/$f"
done
curl -sfL -o vendor/lib/libble_app.a "$BLE/libble_app.a"
for f in esp32c61.rom.ld esp32c61.rom.api.ld esp32c61.rom.coexist.ld \
         esp32c61.rom.net80211.ld esp32c61.rom.pp.ld esp32c61.rom.phy.ld \
         esp32c61.rom.version.ld; do
    curl -sfL -o "vendor/rom/$f" "$ROM/$f"
done
if command -v sha256sum > /dev/null; then
    sha256sum -c SHA256SUMS
else
    shasum -a 256 -c SHA256SUMS
fi
