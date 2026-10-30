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
