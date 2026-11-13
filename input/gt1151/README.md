# Goodix GT1151/GT1158 touch

This driver reads identity and touch reports at seven-bit I2C address `0x14`.
The board must initialize the shared I2C bus before driver initialization.
Calls run in serialized scheduler context. The driver does not change bus
speed, pad routing, reset lines or controller configuration.

Build with `TIKU_DRV_INPUT_GT1151_ENABLE=1` in a firmware profile that includes
the drivers table. `tiku_drv_input_gt1151_identity()` returns the product and
revision bytes. `tiku_drv_input_gt1151_poll()` returns up to ten contacts with
raw X/Y coordinates, strength and tracking ID. The caller applies display
rotation and coordinate bounds.

The parser checks the report checksum, count consistency and duplicate IDs.
It acknowledges complete reports, including malformed ones; failed reads
remain unacknowledged. A failed read or acknowledgement returns an error and
clears the output report. The driver has no heap allocation or VFS nodes.

The register format follows Espressif's
[GT1151 driver](https://github.com/espressif/esp-bsp/blob/ce586f0d6c177810cfa10bee2d4f0e8c61efc840/components/lcd_touch/esp_lcd_touch_gt1151/esp_lcd_touch_gt1151.c).
On the S31-Korvo-1, native I2C identifies product `1158` and polls idle reports
at configured 100/400 kHz rates. Physical touch coordinates and multiple
fingers remain unqualified. TikuBench's `test_gt1151.py` checks independent
wire fixtures, malformed reports, bus failures and rejected parser mutations.
