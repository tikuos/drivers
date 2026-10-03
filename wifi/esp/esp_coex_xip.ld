/*
 * esp_coex_xip.ld - Espressif's coexistence library in the XIP window
 * (ESP32-C61), for builds with both radios: its code and constants run
 * from flash (only the stacks call it, on lines held across flash writes);
 * its data stays in SRAM.
 */

.xip.coex :
{
    *libcoexist.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .iram1 .iram1.*)
    . = ALIGN(4);
} > XIP
