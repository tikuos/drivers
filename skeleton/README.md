# Driver skeleton

Copy this directory to start a new driver:

```
cp -r tikudrivers/skeleton tikudrivers/<class>/<name>
cd tikudrivers/<class>/<name>

# Rename in every file (header, .c, build.mk):
#   tiku_drv_skeleton           ->  tiku_drv_<class>_<name>
#   TIKU_DRV_SKELETON_ENABLE    ->  TIKU_DRV_<CLASS>_<NAME>_ENABLE
#   TIKU_DRV_SKELETON_H_        ->  TIKU_DRV_<CLASS>_<NAME>_H_
```

Then:

1. Implement `init()` (and `deinit()` if you have hardware to undo).
2. If your driver exposes observable state, fill in the
   `vfs_nodes` array and `vfs_mount` name.
3. Edit `tikudrivers/tiku_drv_table.c`: add the `extern` and the
   guarded array entry.
4. Build: `make TIKU_DRV_<CLASS>_<NAME>_ENABLE=1 ...`

The descriptor is the only thing the kernel sees. Everything else
is local to your driver directory.

## Files in this skeleton

- `tiku_drv_skeleton.h` — driver-public API (if any) plus the
  descriptor symbol declaration.
- `tiku_drv_skeleton.c` — descriptor definition, init/deinit,
  VFS handler stubs.
- `build.mk` — Makefile fragment auto-included by the top-level
  Makefile when this directory is under `tikudrivers/`.

## Class choice

Pick the closest match. Adding a new class means extending
`tiku_drv_class_t` in `kernel/drivers/tiku_drv.h` — a coordinated
change with the core repo, so prefer reusing an existing class
when in doubt:

- `SENSOR` — temperature, pressure, humidity, IMU, light, gas
- `RADIO` — sub-GHz transceivers, LoRa, NRF24
- `WIFI`  — 2.4 / 5 GHz IEEE 802.11 chipsets
- `BLE`   — Bluetooth LE radios (separate from WiFi for clarity
  even on combo chips)
- `DISPLAY` — e-paper, LCD, OLED, segment displays
- `STORAGE` — SD/MMC, NOR/NAND flash, FRAM expansion
- `INPUT`  — buttons, rotary encoders, capacitive touch
- `OTHER`  — RTC chips, GPIO expanders, anything else
