# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# sources.mk - Pinned SAE and P-256 sources shared by firmware and host checks.
# SPDX-License-Identifier: Apache-2.0
ESP_SAE_DIR ?= drivers/wifi/esp/sae
ESP_SAE_HOST := $(ESP_SAE_DIR)/vendor/hostap/src
ESP_SAE_MBED := $(ESP_SAE_DIR)/vendor/mbedtls
ESP_SAE_FLAGS := -include $(ESP_SAE_DIR)/esp_sae_config.h \
 -DMBEDTLS_CONFIG_FILE='"esp_sae_config.h"' -I$(ESP_SAE_DIR) \
 -I$(ESP_SAE_HOST) -I$(ESP_SAE_HOST)/utils -I$(ESP_SAE_HOST)/common \
 -I$(ESP_SAE_MBED)/include
ESP_SAE_SRCS := $(ESP_SAE_DIR)/esp_sae_backend.c $(ESP_SAE_DIR)/esp_sae_engine.c \
 $(addprefix $(ESP_SAE_HOST)/,common/sae.c common/dragonfly.c utils/wpabuf.c \
 crypto/sha256.c crypto/sha256-prf.c crypto/sha256-kdf.c) \
 $(addprefix $(ESP_SAE_MBED)/library/,ecp.c ecp_curves.c bignum.c \
 bignum_core.c bignum_mod.c bignum_mod_raw.c constant_time.c platform_util.c)
