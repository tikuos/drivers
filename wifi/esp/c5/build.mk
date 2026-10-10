# C5 PHY calibration and RF lifecycle, independent of the C61 MAC adapters.
ifneq ($(filter 1,$(TIKU_DRV_PHY_C5_ENABLE) $(and $(filter esp32c5,$(MCU)),$(filter 1,$(TIKU_DRV_WIFI_ESP_ENABLE) $(TIKU_DRV_BLE_ESP_ENABLE) $(TIKU_DRV_SDR_ESP_ENABLE)))),)
ifneq ($(MCU),esp32c5)
$(error TIKU_DRV_PHY_C5_ENABLE requires MCU=esp32c5)
endif
C5_PHY_DIR := drivers/wifi/esp/c5
C5_PHY_ASSETS := $(addprefix $(C5_PHY_DIR)/vendor/,libphy.a \
    esp32c5.rom.ld esp32c5.rom.phy.ld esp32c5.rom.version.ld LICENSE-phy)
ifneq ($(words $(wildcard $(C5_PHY_ASSETS))),5)
$(error C5 PHY assets missing: run sh $(C5_PHY_DIR)/fetch.sh)
endif
C5_PHY_HASH_OK := $(shell cd $(C5_PHY_DIR) && \
    { if command -v sha256sum >/dev/null; then sha256sum -c SHA256SUMS; \
      else shasum -a 256 -c SHA256SUMS; fi; } >/dev/null 2>&1 && echo yes)
ifneq ($(C5_PHY_HASH_OK),yes)
$(error C5 PHY asset checksum mismatch: run sh $(C5_PHY_DIR)/fetch.sh)
endif
SRCS += $(C5_PHY_DIR)/tiku_drv_phy_c5.c
ifeq ($(filter 1,$(TIKU_DRV_WIFI_ESP_ENABLE) $(TIKU_DRV_BLE_ESP_ENABLE)),)
SRCS += $(C5_PHY_DIR)/phy_c5_glue.c
endif
CFLAGS += -DTIKU_DRV_PHY_C5_ENABLE=1
LDFLAGS += $(addprefix -T,$(filter %.ld,$(C5_PHY_ASSETS)))
LDLIBS += -Wl,--start-group $(C5_PHY_DIR)/vendor/libphy.a -lc -lgcc -Wl,--end-group
endif

ifeq ($(MCU),esp32c5)
ifeq ($(TIKU_DRV_SDR_ESP_ENABLE),1)
ifneq ($(TIKU_ESP32C5_XIP_CODE),1)
$(error C5 SDR requires TIKU_ESP32C5_XIP_CODE=1)
endif
ifeq ($(TIKU_DRV_SDR_ESP_PROBE),1)
$(error C5 SDR supports receive-only operation; transmit probes are not supported)
endif
ifneq ($(filter 1,$(TIKU_DRV_WIFI_ESP_ENABLE) $(TIKU_DRV_BLE_ESP_ENABLE)),)
$(error C5 SDR is a profile of its own; build it without Wi-Fi or BLE)
endif
SRCS += drivers/wifi/esp/esp_sdr.c $(C5_PHY_DIR)/sdr_c5.c
# Static SRAM must end below the capture bank at 0x40820000; an 8-command
# shell history (2 KB of retained SRAM instead of 4 KB) keeps it there.
CFLAGS += -DTIKU_DRV_SDR_ESP_ENABLE=1 -DTIKU_SHELL_HISTORY_DEPTH=8
LDFLAGS += -Wl,--defsym=__tiku_c5_sdr=1
endif
ifneq ($(filter 1,$(TIKU_DRV_WIFI_ESP_ENABLE) $(TIKU_DRV_BLE_ESP_ENABLE)),)
ifneq ($(TIKU_THREADS_ENABLE):$(TIKU_ESP32C5_XIP_CODE),1:1)
$(error C5 radios require TIKU_THREADS_ENABLE=1 TIKU_ESP32C5_XIP_CODE=1)
endif
SRCS += $(addprefix drivers/wifi/esp/,esp_core.c esp_osi.c esp_heap.c \
    esp_glue.c c5/wifi_c5_arch.c c5/wifi_c5_phy.c)
LDFLAGS += -T$(C5_PHY_DIR)/vendor/esp32c5.rom.api.ld
LDLIBS += -lm
ifeq ($(TIKU_DRV_WIFI_ESP_ENABLE),1)
C5_WIFI_HASH_OK := $(shell cd $(C5_PHY_DIR) && \
    { if command -v sha256sum >/dev/null; then sha256sum -c SHA256SUMS-wifi; \
      else shasum -a 256 -c SHA256SUMS-wifi; fi; } >/dev/null 2>&1 && echo yes)
ifneq ($(C5_WIFI_HASH_OK),yes)
$(error C5 Wi-Fi assets missing or invalid: run sh $(C5_PHY_DIR)/fetch.sh --wifi)
endif
SRCS += $(addprefix drivers/wifi/esp/,tiku_drv_wifi_esp.c esp_wpa.c esp_crypto.c \
    c5/wifi_c5_regulatory.c)
SRCS += $(addprefix tikukits/crypto/,sha1/tiku_kits_crypto_sha1.c \
    hmac/tiku_kits_crypto_hmac_sha1.c pbkdf2/tiku_kits_crypto_pbkdf2.c \
    aes128/tiku_kits_crypto_aes128.c aeskw/tiku_kits_crypto_aeskw.c \
    sha256/tiku_kits_crypto_sha256.c)
CFLAGS += -DTIKU_DRV_WIFI_ESP_ENABLE=1
LDFLAGS += $(addprefix -T$(C5_PHY_DIR)/vendor/esp32c5.rom.,coexist.ld net80211.ld pp.ld)
LDLIBS += -Wl,--start-group $(addprefix $(C5_PHY_DIR)/vendor/,libnet80211.a libpp.a libcore.a libphy.a) -lm -lc -lgcc -Wl,--end-group
endif
ifeq ($(TIKU_DRV_BLE_ESP_ENABLE),1)
C5_BLE_HASH_OK := $(shell cd $(C5_PHY_DIR) && \
    { if command -v sha256sum >/dev/null; then sha256sum -c SHA256SUMS-ble; \
      else shasum -a 256 -c SHA256SUMS-ble; fi; } >/dev/null 2>&1 && echo yes)
ifneq ($(C5_BLE_HASH_OK),yes)
$(error C5 BLE assets missing or invalid: run sh $(C5_PHY_DIR)/fetch.sh --ble)
endif
SRCS += $(addprefix drivers/wifi/esp/,esp_ble.c esp_npl.c esp_mempool.c)
SRCS += tikukits/crypto/p256/tiku_kits_crypto_p256.c
CFLAGS += -DTIKU_DRV_BLE_ESP_ENABLE=1
LDLIBS += -Wl,--start-group $(addprefix $(C5_PHY_DIR)/vendor/,libble_app.a libbtbb.a libphy.a) -lm -lc -lgcc -Wl,--end-group
endif
# Both radios: the coexistence arbiter between them, most of it in ROM
# (esp32c5.rom.coexist.ld, linked with Wi-Fi above), the rest in libcoexist.a.
ifeq ($(TIKU_DRV_WIFI_ESP_ENABLE):$(TIKU_DRV_BLE_ESP_ENABLE),1:1)
C5_COEX_HASH_OK := $(shell cd $(C5_PHY_DIR) && \
    { if command -v sha256sum >/dev/null; then sha256sum -c SHA256SUMS-coex; \
      else shasum -a 256 -c SHA256SUMS-coex; fi; } >/dev/null 2>&1 && echo yes)
ifneq ($(C5_COEX_HASH_OK),yes)
$(error C5 coexistence asset missing or invalid: run sh $(C5_PHY_DIR)/fetch.sh --coex)
endif
SRCS += drivers/wifi/esp/esp_coex.c
LDLIBS += -Wl,--start-group $(C5_PHY_DIR)/vendor/libcoexist.a -lc -lgcc -Wl,--end-group
endif
endif
endif
