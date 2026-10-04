/*
 * esp_sdr_xip.ld - the radio-as-a-receiver driver in the XIP window
 * (ESP32-C61).  It runs only from the shell, never in an interrupt or
 * under a flash write, and its capture bank (SRAM bank 3, 0x40830000)
 * must stay clear of the image: in flash, its code cannot push it there.
 */

.xip.sdr :
{
    *esp_sdr.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(4);
} > XIP
