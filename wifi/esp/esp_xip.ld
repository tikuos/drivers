/*
 * esp_xip.ld - what both radios' libraries share in the XIP window
 * (ESP32-C61): the PHY's code and constants, and the roots a driver keeps
 * whole.  A fragment the arch script INCLUDEs inside its SECTIONS, ahead of
 * .text, after the arch's header (xip_head.ld); the radios' own follow
 * (esp_wifi_xip.ld, esp_ble_xip.ld).  libphy's .iram1 stays in SRAM by the
 * arch script's own pattern; its unwind tables are dropped.
 */

.xip.radio :
{
    KEEP(*(.xip.roots))
    *libphy.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(4);
} > XIP

/DISCARD/ :
{
    *libphy.a:*(.eh_frame .eh_frame.*)
}
