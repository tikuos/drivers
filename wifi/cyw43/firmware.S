/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * firmware.S - CYW43439 firmware blobs embedded into .rodata
 *
 * The Infineon-supplied firmware (43439A0.bin), NVRAM (nvram.bin),
 * and Country Locale Matrix (43439A0_clm.bin) blobs ship as
 * separate binary files under firmware/. This file pulls them into
 * the executable's .rodata segment via the GNU assembler's .incbin
 * directive, so the C side can reference them as symbols without
 * needing a runtime filesystem.
 *
 * Each blob is 4-byte aligned; the matching `*_size` symbol holds
 * the byte length so callers don't depend on linker end symbols.
 *
 * Paths are resolved relative to the project root (where the build
 * runs); see Makefile / build.mk for the assembly include path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

    .syntax unified

    .section .rodata.cyw43_firmware, "a", %progbits
    .balign 4
    .global cyw43_firmware_data
    .type   cyw43_firmware_data, %object
cyw43_firmware_data:
    .incbin "drivers/wifi/cyw43/firmware/43439A0.bin"
    .global cyw43_firmware_data_end
cyw43_firmware_data_end:
    .size   cyw43_firmware_data, . - cyw43_firmware_data

    .section .rodata.cyw43_firmware_size, "a", %progbits
    .balign 4
    .global cyw43_firmware_size
    .type   cyw43_firmware_size, %object
cyw43_firmware_size:
    .word cyw43_firmware_data_end - cyw43_firmware_data
    .size cyw43_firmware_size, . - cyw43_firmware_size

    .section .rodata.cyw43_nvram, "a", %progbits
    .balign 4
    .global cyw43_nvram_data
    .type   cyw43_nvram_data, %object
cyw43_nvram_data:
    .incbin "drivers/wifi/cyw43/firmware/nvram.bin"
    .global cyw43_nvram_data_end
cyw43_nvram_data_end:
    .size   cyw43_nvram_data, . - cyw43_nvram_data

    .section .rodata.cyw43_nvram_size, "a", %progbits
    .balign 4
    .global cyw43_nvram_size
    .type   cyw43_nvram_size, %object
cyw43_nvram_size:
    .word cyw43_nvram_data_end - cyw43_nvram_data
    .size cyw43_nvram_size, . - cyw43_nvram_size

    .section .rodata.cyw43_clm, "a", %progbits
    .balign 4
    .global cyw43_clm_data
    .type   cyw43_clm_data, %object
cyw43_clm_data:
    .incbin "drivers/wifi/cyw43/firmware/43439A0_clm.bin"
    .global cyw43_clm_data_end
cyw43_clm_data_end:
    .size   cyw43_clm_data, . - cyw43_clm_data

    .section .rodata.cyw43_clm_size, "a", %progbits
    .balign 4
    .global cyw43_clm_size
    .type   cyw43_clm_size, %object
cyw43_clm_size:
    .word cyw43_clm_data_end - cyw43_clm_data
    .size cyw43_clm_size, . - cyw43_clm_size
