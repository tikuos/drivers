# TikuOS Drivers

Hardware-touching drivers for TikuOS. Sibling of `tikukits/`; same
"separate repo, optional, slots into the kernel tree via Makefile
probe" model. The kernel detects this directory's presence via
`HAS_DRIVERS=1` and links the descriptor table from
`tiku_drv_table.c`.

Authoritative design doc: `../drivers.md` in the core repo.

## Layout

```
drivers/
├── README.md             (this file)
├── tiku_drv_table.c      (the per-build descriptor list)
├── skeleton/             (copy-paste template for new drivers)
├── wifi/
├── sensors/
├── radio/
├── display/
└── storage/
```

## Adding a driver

1. `cp -r skeleton <class>/<name>` (e.g. `wifi/cyw43`, or
   `sensors/temperature/mcp9808`).
2. Rename `tiku_drv_skeleton` → `tiku_drv_<class>_<name>` in the
   header, source, and `build.mk`. Update the `name` field in
   the descriptor.
3. Implement `init()` and the VFS read/write handlers you need.
4. Edit `tiku_drv_table.c`: add the `extern` declaration and the
   guarded entry inside the array.
5. Build: `make TIKU_DRV_<CLASS>_<NAME>_ENABLE=1 …`

## Conventions

- Symbol prefix: `tiku_drv_<class>_<name>_*` for all functions /
  globals defined by a driver.
- Descriptor symbol: `const tiku_drv_t tiku_drv_<class>_<name>`.
- Header guard: `TIKU_DRV_<CLASS>_<NAME>_H_`.
- Enable flag: `TIKU_DRV_<CLASS>_<NAME>_ENABLE`.
- All driver code returns `TIKU_DRV_OK` (= 0) on success; negative
  for errors. Driver-specific positive codes are allowed but
  discouraged — the kernel only checks zero vs non-zero.

## Driver-specific docs

Each driver carries its own README with status, build flags, and
hardware notes. Direct links:

- [`wifi/cyw43/README.md`](wifi/cyw43/README.md) — CYW43439 (Pi Pico
  2 W). Clean-room C implementation. gSPI transport on PIO,
  SDPCM/CDC/BDC WHD protocol, active scan working against real APs.
