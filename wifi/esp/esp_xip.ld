/*
 * esp_xip.ld - Espressif's radio libraries in the XIP window (ESP32-C61).
 *
 * A fragment the arch script INCLUDEs inside its SECTIONS, ahead of .text.
 * Their code and constant data run from flash, the header first so a stale
 * xip.bin is refused before any call into it.  .iram1 stays in SRAM by the
 * arch script's own pattern; libphy's unwind tables are dropped.
 */

.xip.wifi :
{
    KEEP(*(.xip.header))
    KEEP(*(.xip.roots))
    *libnet80211.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .rodata_wlog* .wifi0iram .wifi0iram.* .wifirxiram .wifirxiram.* .wifiextrairam .wifiextrairam.* .wifislprxiram .wifislprxiram.* .wifislpiram .wifislpiram.*)
    *libpp.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .rodata_wlog* .wifi0iram .wifi0iram.* .wifirxiram .wifirxiram.* .wifiextrairam .wifiextrairam.* .wifislprxiram .wifislprxiram.* .wifislpiram .wifislpiram.*)
    *libcore.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .rodata_wlog*)
    *libphy.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(4);
} > XIP

/DISCARD/ :
{
    *libphy.a:*(.eh_frame .eh_frame.*)
}
