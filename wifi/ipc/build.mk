# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# build.mk - P4 station service over native C6 IPC.
# SPDX-License-Identifier: Apache-2.0
ifeq ($(TIKU_DRV_WIFI_IPC_ENABLE),1)
ifneq ($(MCU):$(BOARD):$(TIKU_IPC_ENABLE):$(TIKU_SDIO_C6_ENABLE),esp32p4:firebeetle2_esp32p4:1:1)
$(error Wi-Fi IPC requires FireBeetle P4 with IPC and native SDIO enabled)
endif
ifneq ($(filter 1,$(TIKU_DRV_WIFI_ESP_ENABLE) $(TIKU_DRV_WIFI_CYW43_ENABLE)),)
$(error Select one wireless provider)
endif
CFLAGS += -DTIKU_DRV_WIFI_IPC_ENABLE=1 -DTIKU_IPC_BUFFERS_MAX=8 -DTIKU_RADIO_LOCAL_QUEUE=4 \
 -DTIKU_KITS_NET_MTU=576 -DTIKU_KITS_NET_TCP_TX_POOL_COUNT=6 \
 -DTIKU_KITS_NET_TCP_MAX_CONNS=2 -DTIKU_KITS_NET_TCP_RX_BUF_SIZE=2048 \
 -DTIKU_KITS_NET_TCP_RTO_INIT=8 \
 -DTIKU_SHELL_READ_MAX=2048
SRCS += drivers/wifi/ipc/tiku_drv_wifi_ipc.c services/ipc/tiku_ipc_radio_codec.c \
 services/ipc/tiku_ipc_radio_frames.c
TIKU_IPC_VFS_NODES_MAX ?= 80
endif
