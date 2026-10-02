# drivers/wifi/esp/build.mk
#
# ESP32-C61 Wi-Fi over Espressif's radio libraries -- opt-in via
# TIKU_DRV_WIFI_ESP_ENABLE.  The libraries and the ROM symbol scripts are not
# tracked in git: fetch.sh downloads them at the commits ESP-IDF pins and
# checks their SHA-256s.  Their code links into the XIP window (esp_xip.ld)
# and `make flash` writes it as xip.bin beside the boot image.

ifeq ($(TIKU_DRV_WIFI_ESP_ENABLE),1)
ifneq ($(MCU),esp32c61)
$(error wifi/esp: the Espressif radio libraries are built for MCU=esp32c61)
endif
ifneq ($(TIKU_THREADS_ENABLE),1)
$(error wifi/esp: the radio's tasks are worker threads -- build with \
TIKU_THREADS_ENABLE=1)
endif
ESPW_DIR     := drivers/wifi/esp
ESPW_LIBS    := $(addprefix $(ESPW_DIR)/vendor/lib/, \
                  libnet80211.a libpp.a libcore.a libphy.a)
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

SRCS    += $(ESPW_DIR)/tiku_drv_wifi_esp.c
SRCS    += $(ESPW_DIR)/esp_osi.c
SRCS    += $(ESPW_DIR)/esp_phy.c
SRCS    += $(ESPW_DIR)/esp_heap.c
SRCS    += $(ESPW_DIR)/esp_wpa.c
SRCS    += $(ESPW_DIR)/esp_crypto.c
SRCS    += $(ESPW_DIR)/esp_glue.c
# The supplicant's and the libraries' crypto, from TikuKits (the crypto kit
# may list them too: SRCS is de-duplicated).
SRCS    += $(addprefix tikukits/crypto/, sha1/tiku_kits_crypto_sha1.c \
             hmac/tiku_kits_crypto_hmac_sha1.c pbkdf2/tiku_kits_crypto_pbkdf2.c \
             aes128/tiku_kits_crypto_aes128.c aeskw/tiku_kits_crypto_aeskw.c \
             sha256/tiku_kits_crypto_sha256.c)
CFLAGS  += -DTIKU_DRV_WIFI_ESP_ENABLE=1
# esp_xip.ld joins the arch script's XIP fragments; the ROM scripts only name
# addresses.  The libraries get their own group so their libc calls resolve
# whatever order the kernel's group left.
TIKU_XIP_LDS += $(ESPW_DIR)/esp_xip.ld
LDFLAGS += $(addprefix -T,$(ESPW_ROMLDS))
LDLIBS  += -Wl,--start-group $(ESPW_LIBS) -lm -lc -lgcc -Wl,--end-group
endif
