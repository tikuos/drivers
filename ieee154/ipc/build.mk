# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# build.mk - P4 IEEE 802.15.4 through the C6 radio mailbox.
# SPDX-License-Identifier: Apache-2.0
ifeq ($(TIKU_DRV_154_IPC_ENABLE),1)
ifneq ($(MCU):$(BOARD):$(TIKU_DRV_WIFI_IPC_ENABLE):$(TIKU_DRV_BLE_IPC_ENABLE),esp32p4:firebeetle2_esp32p4:1:1)
$(error IEEE 802.15.4 IPC requires the FireBeetle P4 Wi-Fi and BLE mailbox profile)
endif
CFLAGS += -DTIKU_DRV_154_IPC_ENABLE=1 -DTIKU_HAS_154=1
TIKU_CAP_154 := 1
SRCS += drivers/ieee154/ipc/tiku_drv_154_ipc.c services/ipc/tiku_ipc_154_codec.c \
    interfaces/radio/tiku_154.c interfaces/radio/tiku_154_frame.c \
    tikukits/crypto/ccm/tiku_kits_crypto_ccm.c tikukits/crypto/aes128/tiku_kits_crypto_aes128.c
endif
