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

The controller writes reports while a finger is on the panel, one report
with no contacts when the last finger lifts, and none after that. A report
sets bit 7 of the status register at `0x814e`, and the driver's
acknowledgement clears the register. A poll that finds bit 7 clear returns
`fresh` 0 and no contacts: the controller has written nothing since the last
poll, and the caller keeps the contacts it has. A caller that polls faster
than the controller reports sees such polls between the reports of one held
finger, and must not take them for a lift.

The parser checks the report checksum, count consistency and duplicate IDs.
It acknowledges complete reports, including malformed ones; failed reads
remain unacknowledged, and a poll without a report writes nothing. A failed read or acknowledgement returns an error and
clears the output report. The driver has no heap allocation or VFS nodes.

The register format follows Espressif's
[GT1151 driver](https://github.com/espressif/esp-bsp/blob/ce586f0d6c177810cfa10bee2d4f0e8c61efc840/components/lcd_touch/esp_lcd_touch_gt1151/esp_lcd_touch_gt1151.c).
On the S31-Korvo-1, native I2C identifies product `1158` and polls idle reports
at configured 100/400 kHz rates. Polled 119 times a second there, a finger
gave about 108 reports a second and the other polls found none; 29 lifts each
arrived as a report with no contacts. Multiple fingers remain unqualified. TikuBench's `test_gt1151.py` checks independent
wire fixtures, malformed reports, bus failures and rejected parser mutations.
