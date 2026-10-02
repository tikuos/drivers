/*
 * esp_ble_xip.ld - Espressif's BLE controller and baseband in the XIP
 * window (ESP32-C61), placed as IDF's run-in-flash-only mode places them:
 * code, constants, and the .iram1 and fast-advertising code IDF otherwise
 * keeps in RAM.  .high_perf_code_iram1 and all their data stay in SRAM by
 * the arch script's patterns; their unwind tables are dropped.
 */

.xip.ble :
{
    *libble_app.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .iram1 .iram1.* .adv_fast_execute_code_iram1 .adv_fast_execute_code_iram1.* .iram_sleep .iram_sleep.* .iram_btslp .iram_btslp.* .iram_sleep_adv .iram_sleep_adv.* .iram_sleep_conn .iram_sleep_conn.* .iram_single_sleep .iram_single_sleep.*)
    *libbtbb.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .iram1 .iram1.*)
    . = ALIGN(4);
} > XIP

/DISCARD/ :
{
    *libble_app.a:*(.eh_frame .eh_frame.*)
    *libbtbb.a:*(.eh_frame .eh_frame.*)
}
