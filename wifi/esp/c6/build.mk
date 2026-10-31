# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# build.mk - Native C6 station adapter with pinned Wi-Fi and PHY archives.
# SPDX-License-Identifier: Apache-2.0
ifeq ($(MCU),esp32c6)
ifneq ($(filter 1,$(TIKU_DRV_WIFI_ESP_ENABLE) $(TIKU_DRV_BLE_ESP_ENABLE)),)
ifneq ($(C6_PROFILE):$(TIKU_SHELL_ENABLE):$(C6_STORAGE):$(TIKU_THREADS_ENABLE),firmware:1:1:1)
$(error C6 radios require firmware, shell, storage and workers enabled)
endif
ifneq ($(filter 1,$(TIKU_DRV_SDR_ESP_ENABLE)),)
$(error The C6 radio profile excludes SDR)
endif
C6_WIFI_DIR := drivers/wifi/esp/c6
C6_LDFLAGS += -Wl,--wrap=free
C6_WIFI_ASSETS_OK := $(shell cd $(C6_WIFI_DIR) && sha256sum -c SHA256SUMS >/dev/null 2>&1 && echo yes)
ifneq ($(C6_WIFI_ASSETS_OK),yes)
$(error C6 Wi-Fi assets missing or invalid: run sh $(C6_WIFI_DIR)/fetch.sh)
endif
CFLAGS += -DHAS_DRIVERS=1 -DHAS_TIKUKITS=1 -DTIKU_C6_IRQ_LAST=31
C6_SRCS += drivers/tiku_drv_table.c $(addprefix drivers/wifi/esp/,esp_core.c esp_osi.c esp_heap.c \
    esp_glue.c c6/wifi_c6_arch.c c6/wifi_c6_phy.c)
ifeq ($(TIKU_DRV_WIFI_ESP_ENABLE),1)
CFLAGS += -DTIKU_DRV_WIFI_ESP_ENABLE=1 -DTIKU_WIRELESS_AP_ENABLE=1
C6_SRCS += $(addprefix drivers/wifi/esp/,esp_wpa.c esp_ap.c esp_crypto.c tiku_drv_wifi_esp.c)
include drivers/wifi/esp/sae/sources.mk
ESP_SAE_ASSETS_OK := $(shell cd $(ESP_SAE_DIR) && sha256sum -c SHA256SUMS >/dev/null 2>&1 && echo yes)
ifneq ($(ESP_SAE_ASSETS_OK),yes)
$(error SAE sources missing or invalid: run python3 $(ESP_SAE_DIR)/fetch.py)
endif
CFLAGS += -DTIKU_ESP_SAE_ENABLE=1
C6_SRCS += drivers/wifi/esp/esp_sae.c $(ESP_SAE_SRCS)
$(addprefix $(BUILD_DIR)/,$(ESP_SAE_SRCS:.c=.o)): CFLAGS += $(ESP_SAE_FLAGS) -Wno-unused-parameter
C6_SRCS += shell/commands/tiku_shell_cmd_wifi.c
C6_SRCS += $(addprefix tikukits/crypto/,sha1/tiku_kits_crypto_sha1.c \
    hmac/tiku_kits_crypto_hmac_sha1.c pbkdf2/tiku_kits_crypto_pbkdf2.c \
    aes128/tiku_kits_crypto_aes128.c aeskw/tiku_kits_crypto_aeskw.c sha256/tiku_kits_crypto_sha256.c ccm/tiku_kits_crypto_ccm.c)
endif
ifeq ($(TIKU_DRV_BLE_ESP_ENABLE),1)
include $(C6_WIFI_DIR)/ble.mk
endif
C6_SRCS := $(sort $(C6_SRCS))
C6_LDFLAGS += $(addprefix -T$(C6_WIFI_DIR)/vendor/,esp32c6.rom.ld esp32c6.rom.api.ld \
    esp32c6.rom.phy.ld esp32c6.rom.coexist.ld esp32c6.rom.net80211.ld esp32c6.rom.pp.ld)
C6_LIBS += -Wl,--start-group $(addprefix $(C6_WIFI_DIR)/vendor/,libnet80211.a libpp.a libcore.a libphy.a) \
    -lm -lc_nano -lgcc -Wl,--end-group
C6_LINK_DEPS += $(C6_WIFI_DIR)/wifi_c6_xip.ld $(wildcard $(C6_WIFI_DIR)/vendor/*.a) \
    $(wildcard $(C6_WIFI_DIR)/vendor/*.ld)
ifeq ($(TIKU_KIT_NET_ENABLE),1)
CFLAGS += -DTIKU_KIT_NET_ENABLE=1 -DTIKU_KIT_NET_MIN=1 -DTIKU_KITS_NET_WIFI_ENABLE=1 \
    -DTIKU_KITS_NET_DHCP_ENABLE=1 -DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_KITS_NET_MTU=576 \
    -DTIKU_SHELL_CMD_SLIP=0 -DTIKU_SHELL_CMD_NTP=0 -DTIKU_SHELL_CMD_DNS=0
C6_SRCS += $(addprefix tikukits/net/ipv4/tiku_kits_net_,ipv4.c icmp.c udp.c dhcp.c tcp.c)
C6_SRCS += tikukits/net/wifi/tiku_kits_net_wifi.c tikukits/net/slip/tiku_kits_net_slip.c
C6_SRCS += shell/commands/tiku_shell_cmd_ip.c shell/commands/tiku_shell_cmd_ping.c \
    shell/commands/tiku_shell_cmd_slip.c
endif
endif
endif
