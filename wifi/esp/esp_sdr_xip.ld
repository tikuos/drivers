/*
 * esp_sdr_xip.ld - the radio-as-a-receiver driver in the XIP window
 * (ESP32-C61).  It runs only from the shell, never in an interrupt or
 * under a flash write, and its capture bank (SRAM bank 3, 0x40830000)
 * must stay clear of the image: in flash, its code cannot push it there,
 * and the link fails if the rest of the image grows into it.
 */

.xip.sdr :
{
    *esp_sdr.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(4);
} > XIP

ASSERT(__tier_sram_start <= 0x40830000,
       "the image reaches SRAM bank 3, the SDR's capture bank: run more kernel code from flash (arch/esp32c61/xip_code.ld)")
