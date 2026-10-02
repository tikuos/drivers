/*
 * esp_wifi_xip.ld - Espressif's Wi-Fi libraries in the XIP window
 * (ESP32-C61): their code and constant data run from flash.  .iram1 stays
 * in SRAM by the arch script's own pattern.
 */

.xip.wifi :
{
    *libnet80211.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .rodata_wlog* .wifi0iram .wifi0iram.* .wifirxiram .wifirxiram.* .wifiextrairam .wifiextrairam.* .wifislprxiram .wifislprxiram.* .wifislpiram .wifislpiram.*)
    *libpp.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .rodata_wlog* .wifi0iram .wifi0iram.* .wifirxiram .wifirxiram.* .wifiextrairam .wifiextrairam.* .wifislprxiram .wifislprxiram.* .wifislpiram .wifislpiram.*)
    *libcore.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .rodata_wlog*)
    . = ALIGN(4);
} > XIP
