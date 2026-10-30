# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# build.mk - P4 Bluetooth host over the C6 mailbox service.
# SPDX-License-Identifier: Apache-2.0
ifeq ($(TIKU_DRV_BLE_IPC_ENABLE),1)
ifneq ($(MCU):$(BOARD):$(TIKU_DRV_WIFI_IPC_ENABLE),esp32p4:firebeetle2_esp32p4:1)
$(error BLE IPC requires the FireBeetle P4 radio-service profile)
endif
ifneq ($(filter 1,$(TIKU_DRV_BLE_ESP_ENABLE) $(TIKU_DRV_BLE_EM9305_ENABLE) $(TIKU_DRV_BLE_FLPR_ENABLE) $(TIKU_DRV_WIFI_CYW43_BT_ENABLE)),)
$(error Select one Bluetooth controller provider)
endif
CFLAGS += -DTIKU_DRV_BLE_IPC_ENABLE=1
SRCS += drivers/ble/ipc/tiku_drv_ble_ipc.c services/ipc/tiku_ipc_ble_packets.c
endif
