# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# ble.mk - Native C6 controller and Bluetooth host selection.
# SPDX-License-Identifier: Apache-2.0
C6_BLE_ASSETS_OK := $(shell cd $(C6_WIFI_DIR) && sha256sum -c SHA256SUMS-ble >/dev/null 2>&1 && echo yes)
ifneq ($(C6_BLE_ASSETS_OK),yes)
$(error C6 BLE assets missing or invalid: run sh $(C6_WIFI_DIR)/fetch.sh --ble)
endif
ifeq ($(TIKU_DRV_WIFI_ESP_ENABLE),1)
CFLAGS += -DTIKU_DRV_ESP_COEX_HEAP_BYTES=65536
C6_SRCS += drivers/wifi/esp/esp_coex.c
C6_LIBS += $(C6_WIFI_DIR)/vendor/libcoexist.a
endif
CFLAGS += -include$(C6_WIFI_DIR)/ble_config.h -DTIKU_DRV_BLE_ESP_ENABLE=1 -DTIKU_BT_HOST=1 -DTIKU_BT_ON_DEMAND=1
C6_SRCS += $(addprefix drivers/wifi/esp/,esp_ble.c esp_npl.c esp_mempool.c)
C6_SRCS += tikukits/crypto/p256/tiku_kits_crypto_p256.c tikukits/net/bluetooth/tiku_bt.c
C6_SRCS += shell/commands/tiku_shell_cmd_bt.c tikukits/crypto/sha256/tiku_kits_crypto_sha256.c
C6_LIBS += -Wl,--start-group $(C6_WIFI_DIR)/vendor/libble_app.a $(C6_WIFI_DIR)/vendor/libbtbb.a -Wl,--end-group
