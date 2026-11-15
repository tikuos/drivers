# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# ble.mk - S31 native BLE controller build.
# SPDX-License-Identifier: Apache-2.0
ifndef S31_BT_IDF
$(error S31_BLE_ENABLE=1 requires S31_BT_IDF; see drivers/wifi/esp/s31/README.md)
endif
ifndef S31_BT_LIB_DIR
$(error S31_BLE_ENABLE=1 requires S31_BT_LIB_DIR; see drivers/wifi/esp/s31/README.md)
endif
ifneq ($(S31_PROFILE),firmware)
$(error S31_BLE_ENABLE=1 requires S31_PROFILE=firmware)
endif
S31_BT_ASSET_CHECK := $(shell $(PYTHON) drivers/wifi/esp/s31/check_ble_assets.py --sdk "$(S31_BT_IDF)" --controller "$(S31_BT_LIB_DIR)" 2>&1)
ifneq ($(strip $(S31_BT_ASSET_CHECK)),)
$(error $(S31_BT_ASSET_CHECK))
endif
S31_BT_ROOT := $(S31_BT_IDF)/components/bt
S31_BT_PORT := $(S31_BT_ROOT)/porting_btdm
S31_BT_SRCS := $(S31_BT_ROOT)/controller/esp32s31/bt.c \
    $(S31_BT_ROOT)/controller/esp32s31/btdm_broker.c \
    $(S31_BT_PORT)/controller/ble/src/ble.c \
    $(S31_BT_PORT)/controller/ble/src/ble_msys.c \
    $(S31_BT_PORT)/controller/btdm_common/src/btdm_coex.c \
    $(S31_BT_PORT)/controller/btdm_common/src/btdm_external.c \
    $(S31_BT_PORT)/controller/btdm_common/src/btdm_log.c \
    $(S31_BT_PORT)/transport/src/hci_transport.c \
    $(S31_BT_PORT)/transport/src/hci_cmd_proc.c
S31_BT_INCLUDES := -Idrivers/wifi/esp/s31/ble_compat \
    -I$(S31_BT_ROOT)/include/esp32s31/include \
    -I$(S31_BT_PORT)/controller/ble/include \
    -I$(S31_BT_PORT)/controller/bredr/include \
    -I$(S31_BT_PORT)/controller/btdm_common/include \
    -I$(S31_BT_PORT)/transport/include \
    -I$(S31_BT_IDF)/components/esp_hw_support/include \
    -I$(S31_BT_IDF)/components/hal/include \
    -I$(S31_BT_IDF)/components/hal/esp32s31/include \
    -I$(S31_BT_IDF)/components/esp_common/include \
    -I$(S31_BT_IDF)/components/soc/esp32s31/include \
    -I$(S31_BT_IDF)/components/soc/include \
    -I$(S31_BT_IDF)/components/esp_rom/esp32s31/include
S31_FLAGS += -DTIKU_S31_BLE_ENABLE=1
S31_SRCS += $(S31_BT_SRCS) drivers/wifi/esp/esp_npl.c drivers/wifi/esp/esp_mempool.c \
    drivers/wifi/esp/s31/btdm_s31_osal.c drivers/wifi/esp/s31/btdm_s31_mempool.c \
    drivers/wifi/esp/s31/ble_s31.c drivers/wifi/esp/s31/ble_s31_platform.c
$(addprefix $(S31_DIR)/,$(S31_BT_SRCS:.c=.o) drivers/wifi/esp/s31/ble_s31.o drivers/wifi/esp/s31/ble_s31_platform.o): S31_FLAGS += $(S31_BT_INCLUDES) -include drivers/wifi/esp/esp_heap.h -Dmalloc=espw_malloc -Dcalloc=espw_calloc -Dfree=espw_free -Drealloc=espw_realloc -Wno-sign-compare -Wno-unused-parameter -Wno-unused-variable -Wno-cast-function-type -Wno-missing-field-initializers
S31_LIBS += $(S31_BT_LIB_DIR)/libbtdm_common.a \
    $(S31_BT_LIB_DIR)/libble_app.a \
    drivers/wifi/esp/s31/vendor/libbtbb.a

S31_LDFLAGS += -Wl,--defsym=S31_BLE=1
S31_LDFLAGS += -Wl,--wrap=btdm_osal_elem_mempool_init

ifeq ($(S31_RADIO_COEX),1)
S31_FLAGS += -DTIKU_S31_RADIO_COEX=1 -DTIKU_DRV_WIFI_ESP_PSRAM_BYTES=0
S31_BT_INCLUDES += -I$(S31_BT_IDF)/components/esp_coex/include \
    -I$(S31_BT_IDF)/components/esp_hal_gpio/include \
    -I$(S31_BT_IDF)/components/esp_hal_gpio/esp32s31/include
S31_SRCS += drivers/wifi/esp/esp_coex.c
S31_LIBS += drivers/wifi/esp/s31/vendor/libcoexist.a
endif

S31_FLAGS += -DTIKU_BT_HOST=1 -DTIKU_BT_ON_DEMAND=1
S31_SRCS += tikukits/net/bluetooth/tiku_bt.c interfaces/bluetooth/tiku_ble_adv.c interfaces/bluetooth/tiku_ble_serial.c shell/commands/tiku_shell_cmd_bt.c
$(S31_DIR)/tikukits/net/bluetooth/tiku_bt.o: S31_FLAGS += -DTIKU_BT_PRINTF=printf -include stdio.h -Wno-unused-function

S31_FLAGS += -DTIKU_BT_CONN_MAX=2 -DTIKU_BT_EXTENDED=1 -DTIKU_BT_ADV_SET_MAX=2 -DTIKU_BT_SW_CRYPTO=1
S31_SRCS += tikukits/crypto/p256/tiku_kits_crypto_p256.c
