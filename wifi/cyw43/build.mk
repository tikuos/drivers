# tikudrivers/wifi/cyw43/build.mk
#
# CYW43439 WiFi driver — opt-in via TIKU_DRV_WIFI_CYW43_ENABLE.
# When the flag is unset the driver contributes zero code to the
# image. See tikudrivers/wifi/cyw43/README.md for the build flow.

ifeq ($(TIKU_DRV_WIFI_CYW43_ENABLE),1)
SRCS     += $(wildcard tikudrivers/wifi/cyw43/*.c)
# firmware.S pulls in 43439A0.bin (~225 KB), nvram.bin (~750 B), and
# 43439A0_clm.bin (~1 KB) via .incbin. The chip's own Cortex-M3 needs
# the firmware blob uploaded after every reset (the chip has no
# non-volatile storage), so the blob lives in RP2350 flash and the
# driver streams it across gSPI on every boot.
ASM_SRCS += tikudrivers/wifi/cyw43/firmware.S
CFLAGS   += -DTIKU_DRV_WIFI_CYW43_ENABLE=1
endif
