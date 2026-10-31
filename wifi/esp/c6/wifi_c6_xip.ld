/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * wifi_c6_xip.ld - Flash code and constants inside the C6 application slot.
 * SPDX-License-Identifier: Apache-2.0
 */
*esp_*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*wifi_c6_*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*tiku_drv_wifi_esp.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*tiku_kits_crypto_*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*tiku_kits_net_*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*tiku_vfs*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*libnet80211.a:*(.text .text.* .rodata* .srodata* .wifi0iram* .wifirxiram* .wifiextrairam* .wifislp*)
*libpp.a:*(.text .text.* .rodata* .srodata* .wifi0iram* .wifirxiram* .wifiextrairam* .wifislp*)
*libcore.a:*(.text .text.* .rodata* .srodata*)
*libphy.a:*(.text .text.* .rodata* .srodata*)
KEEP(*(.xip.roots))
*tiku_bt.o(.text .text.* .rodata* .srodata*)
*libble_app.a:*(.text .text.* .rodata* .srodata* .iram1 .iram1.* .adv_fast_execute_code_iram1* .iram_sleep* .iram_btslp* .iram_single_sleep*)
*libbtbb.a:*(.text .text.* .rodata* .srodata* .iram1 .iram1.*)
*libcoexist.a:*(.text .text.* .rodata* .srodata* .iram1 .iram1.*)
*sae.o(.text .text.* .rodata* .srodata*)
*dragonfly.o(.text .text.* .rodata* .srodata*)
*wpabuf.o(.text .text.* .rodata* .srodata*)
*sha256*.o(.text .text.* .rodata* .srodata*)
*ecp*.o(.text .text.* .rodata* .srodata*)
*bignum*.o(.text .text.* .rodata* .srodata*)
*constant_time.o(.text .text.* .rodata* .srodata*)
*platform_util.o(.text .text.* .rodata* .srodata*)
*tiku_ipc_radio*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
