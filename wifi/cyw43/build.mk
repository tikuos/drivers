# drivers/wifi/cyw43/build.mk
#
# CYW43439 WiFi driver — opt-in via TIKU_DRV_WIFI_CYW43_ENABLE.
# When the flag is unset the driver contributes zero code to the
# image. See drivers/wifi/cyw43/README.md for the build flow.
#
# Optional BT extension: TIKU_DRV_WIFI_CYW43_BT_ENABLE adds the
# CYW43439 BTSDIO transport (bt_transport.c) on top of the WiFi
# driver. The chip is dual-mode so the BT transport shares this
# directory, but it requires WiFi to be enabled too (the WHD firmware
# must be running before the BT side can be powered up). The
# transport plugs into the generic BLE protocol stack in
# tikukits/net/bluetooth/.

ifeq ($(TIKU_DRV_WIFI_CYW43_ENABLE),1)
# The Infineon firmware blobs are NOT tracked in git (binary-only PBL
# licence; the repos ship only Apache-2.0 content). Fail early with
# instructions instead of a cryptic .incbin assembler error. `clean`
# is exempt, mirroring the APP=net guard in the top-level Makefile.
CYW43_FW_DIR   := drivers/wifi/cyw43/firmware
CYW43_FW_FILES := $(CYW43_FW_DIR)/43439A0.bin \
                  $(CYW43_FW_DIR)/43439A0_clm.bin \
                  $(CYW43_FW_DIR)/43439A0_btfw.bin \
                  $(CYW43_FW_DIR)/nvram.bin
CYW43_FW_MISSING := $(filter-out $(wildcard $(CYW43_FW_FILES)),$(CYW43_FW_FILES))
ifneq ($(CYW43_FW_MISSING),)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(error cyw43: firmware blob(s) missing: $(CYW43_FW_MISSING) -- not \
tracked in git. See $(CYW43_FW_DIR)/README.md for the download + \
SHA-256 verification commands (Infineon PBL-licensed binaries))
endif
endif

# Explicit source list rather than $(wildcard) so we can gate
# individual files (bt.c) on sub-flags.
SRCS     += drivers/wifi/cyw43/tiku_drv_wifi_cyw43.c
SRCS     += drivers/wifi/cyw43/gspi.c
SRCS     += drivers/wifi/cyw43/whd.c

# firmware.S pulls in 43439A0.bin (~225 KB), nvram.bin (~750 B),
# 43439A0_clm.bin (~1 KB), and 43439A0_btfw.bin (~6 KB) via .incbin.
# The chip's own Cortex-M3 needs the firmware blob uploaded after
# every reset (the chip has no non-volatile storage), so the blobs
# live in RP2350 flash and the driver streams them across gSPI on
# every boot. The BT blob is included unconditionally — at 6 KB the
# cost is negligible and keeping the .S monolithic avoids juggling
# .incbin paths under conditionals.
ASM_SRCS += drivers/wifi/cyw43/firmware.S
CFLAGS   += -DTIKU_DRV_WIFI_CYW43_ENABLE=1

ifeq ($(TIKU_DRV_WIFI_CYW43_BT_ENABLE),1)
SRCS   += drivers/wifi/cyw43/bt_transport.c
CFLAGS += -DTIKU_DRV_WIFI_CYW43_BT_ENABLE=1
endif
endif
