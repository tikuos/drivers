# drivers/wifi/esp/build.mk
#
# ESP32-C61 radios over Espressif's libraries, opt-in: Wi-Fi with
# TIKU_DRV_WIFI_ESP_ENABLE, BLE with TIKU_DRV_BLE_ESP_ENABLE, either or both.
# The libraries and the ROM symbol scripts are not tracked in git: fetch.sh
# downloads them at the commits ESP-IDF pins and checks their SHA-256s.
# Their code links into the XIP window (the esp_*xip.ld fragments) and
# `make flash` writes it as xip.bin beside the boot image.

ifneq ($(filter 1,$(TIKU_DRV_WIFI_ESP_ENABLE) $(TIKU_DRV_BLE_ESP_ENABLE)),)
ifneq ($(MCU),esp32c61)
$(error wifi/esp: the Espressif radio libraries are built for MCU=esp32c61)
endif
ifneq ($(TIKU_THREADS_ENABLE),1)
$(error wifi/esp: the radios' tasks are worker threads -- build with \
TIKU_THREADS_ENABLE=1)
endif
ESPW_DIR     := drivers/wifi/esp
ESPW_LIBS    := libphy.a
ifeq ($(TIKU_DRV_WIFI_ESP_ENABLE),1)
ESPW_LIBS    += libnet80211.a libpp.a libcore.a
endif
ifeq ($(TIKU_DRV_BLE_ESP_ENABLE),1)
ESPW_LIBS    += libble_app.a libbtbb.a
endif
ESPW_COEX    := $(and $(filter 1,$(TIKU_DRV_WIFI_ESP_ENABLE)),$(filter 1,$(TIKU_DRV_BLE_ESP_ENABLE)))
ifneq ($(ESPW_COEX),)
ESPW_LIBS    += libcoexist.a
endif
ESPW_LIBS    := $(addprefix $(ESPW_DIR)/vendor/lib/,$(ESPW_LIBS))
ESPW_ROMLDS  := $(addprefix $(ESPW_DIR)/vendor/rom/esp32c61.rom, \
                  .ld .api.ld .coexist.ld .net80211.ld .pp.ld .phy.ld .version.ld)
ESPW_MISSING := $(filter-out $(wildcard $(ESPW_LIBS) $(ESPW_ROMLDS)), \
                  $(ESPW_LIBS) $(ESPW_ROMLDS))
ifneq ($(strip $(ESPW_MISSING)),)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(error wifi/esp: not fetched: $(ESPW_MISSING) -- run sh $(ESPW_DIR)/fetch.sh \
(see $(ESPW_DIR)/README.md))
endif
endif

# What both radios stand on: the OS adapter, the modem and the PHY, the
# heap, the libraries' surroundings.
SRCS    += $(ESPW_DIR)/esp_core.c
SRCS    += $(ESPW_DIR)/esp_osi.c
SRCS    += $(ESPW_DIR)/esp_phy.c
SRCS    += $(ESPW_DIR)/esp_heap.c
SRCS    += $(ESPW_DIR)/esp_glue.c
TIKU_XIP_LDS += $(ESPW_DIR)/esp_xip.ld

ifeq ($(TIKU_DRV_WIFI_ESP_ENABLE),1)
SRCS    += $(ESPW_DIR)/tiku_drv_wifi_esp.c
SRCS    += $(ESPW_DIR)/esp_wpa.c
SRCS    += $(ESPW_DIR)/esp_crypto.c
# The supplicant's and the libraries' crypto, from TikuKits (the crypto kit
# may list them too: SRCS is de-duplicated).
SRCS    += $(addprefix tikukits/crypto/, sha1/tiku_kits_crypto_sha1.c \
             hmac/tiku_kits_crypto_hmac_sha1.c pbkdf2/tiku_kits_crypto_pbkdf2.c \
             aes128/tiku_kits_crypto_aes128.c aeskw/tiku_kits_crypto_aeskw.c \
             sha256/tiku_kits_crypto_sha256.c)
CFLAGS  += -DTIKU_DRV_WIFI_ESP_ENABLE=1
TIKU_XIP_LDS += $(ESPW_DIR)/esp_wifi_xip.ld
endif

ifeq ($(TIKU_DRV_BLE_ESP_ENABLE),1)
SRCS    += $(ESPW_DIR)/esp_ble.c
SRCS    += $(ESPW_DIR)/esp_npl.c
SRCS    += $(ESPW_DIR)/esp_mempool.c
# The controller's P-256 (LE Secure Connections keys), from TikuKits.
SRCS    += tikukits/crypto/p256/tiku_kits_crypto_p256.c
CFLAGS  += -DTIKU_DRV_BLE_ESP_ENABLE=1
TIKU_XIP_LDS += $(ESPW_DIR)/esp_ble_xip.ld
endif

# The radio as a receiver: raw I/Q snapshots through the PHY the Wi-Fi
# driver brings up.
ifeq ($(TIKU_DRV_SDR_ESP_ENABLE),1)
ifneq ($(TIKU_DRV_WIFI_ESP_ENABLE),1)
$(error TIKU_DRV_SDR_ESP_ENABLE needs TIKU_DRV_WIFI_ESP_ENABLE=1)
endif
SRCS    += $(ESPW_DIR)/esp_sdr.c
CFLAGS  += -DTIKU_DRV_SDR_ESP_ENABLE=1
TIKU_XIP_LDS += $(ESPW_DIR)/esp_sdr_xip.ld
# Its transmit side's lab probes, only when asked for.
ifeq ($(TIKU_DRV_SDR_ESP_PROBE),1)
CFLAGS  += -DTIKU_DRV_SDR_ESP_PROBE=1
endif
endif

# Both radios: the coexistence arbiter between them (most of it in ROM).
ifneq ($(ESPW_COEX),)
SRCS    += $(ESPW_DIR)/esp_coex.c
TIKU_XIP_LDS += $(ESPW_DIR)/esp_coex_xip.ld
endif

# The ROM scripts only name addresses.  The libraries get their own group
# so their libc calls resolve whatever order the kernel's group left.
LDFLAGS += $(addprefix -T,$(ESPW_ROMLDS))
LDLIBS  += -Wl,--start-group $(ESPW_LIBS) -lm -lc -lgcc -Wl,--end-group
endif
