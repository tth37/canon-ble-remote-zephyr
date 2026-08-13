# Repository guidance for coding agents

## Start here

Before changing code, read:

- `README.md` for supported hardware, wiring, build profiles, commands, and
  user-visible behavior.
- `docs/architecture.md` for module boundaries, the Canon BLE lifecycle,
  physical-control behavior, and power-management design.

Inspect `git status` before editing. Preserve all existing user changes and do
not overwrite, reset, or clean files that are outside the requested work.

This repository contains the Canon remote application. Work intended for an
upstream Zephyr contribution belongs in its separate checkout and must not be
added here. The application-local Zephyr patch under `patches/zephyr/` remains
the source of truth for this repository until upstream integration is complete.

## Project boundaries

- Keep one Zephyr application and one shared source set for every supported
  board.
- Express pins and peripherals in board Devicetree overlays.
- Express board policy in board Kconfig fragments and debug/release policy in
  the profile configuration files.
- Keep Canon protocol, BLE lifecycle, peer storage, and bonding behavior inside
  `firmware/src/canon`. Its public boundary is `remote.h`.
- Keep the shell, buttons, display, indicator, and power management as adapters
  around the Canon module. Do not make the shell a runtime dependency.
- Do not introduce application-level board dispatch when Zephyr Devicetree or
  Kconfig can represent the variation.
- Do not edit the ignored `.zephyr` checkout by hand. `make setup` applies the
  pinned patch automatically and idempotently.

## Build and verification

Use the root Makefile rather than invoking an unrelated build framework.
`make setup` owns the repository-local West workspace, Python environment,
Zephyr SDK, and required blobs.

Supported combinations are:

```sh
make build BOARD=esp32c6_devkitc/esp32c6/hpcore PROFILE=debug
make build BOARD=esp32c6_devkitc/esp32c6/hpcore PROFILE=release
make build BOARD=promicro_nrf52840/nrf52840/uf2 PROFILE=debug
```

Use `make menuconfig` for the persistent local selection or pass `BOARD` and
`PROFILE` explicitly for reproducible checks. Build every affected combination
before declaring a firmware change complete. For documentation-only changes,
check the diff and formatting; a firmware rebuild is unnecessary.

The ESP32-C6 debug profile retains UART logging and the shell and deliberately
stays awake. The ESP32-C6 release profile removes diagnostics and enters deep
sleep after its idle timeout. The nRF52840 currently supports only the debug
profile.

## Hardware collaboration

Building does not authorize flashing. Flash only when the user explicitly asks
for it or has clearly said that the connected board may be flashed. Do not
silently choose a serial port when more than one plausible device is present.

When a physical step is required:

1. Tell the user exactly which board, wiring, reset action, or camera mode is
   needed and why.
2. Ask the user to complete it and reply `ready`.
3. Wait for that confirmation before flashing or starting the dependent test.
4. State the expected LED, serial, camera, or reconnect behavior so the user can
   report an unambiguous result.

For an nRF52840 bootloader operation, describe the required reset sequence
before waiting for `ready`. Never assume that a reset, mount, or port observed
in an earlier session still exists.

## Canon pairing and regression checks

Do not erase bonds, run `camera forget`, or force the user to re-pair unless the
requested test requires it and the user understands the consequence.

For a complete pairing regression test:

1. Ask the user to put the camera in Bluetooth Remote pairing mode.
2. Pair through the dedicated physical PAIR button or `camera pair` in an
   ESP32-C6 debug build.
3. Verify focus and shutter press/release behavior.
4. Restart the camera.
5. Verify bonded reconnect, focus, and shutter without pairing again.

Treat the final restart-and-reconnect check as essential for identity or bond
changes. Preserve the fixed-identity behavior on nRF52840 unless the application
patch is intentionally being replaced with a verified upstream equivalent.

Physical GPIO handlers must use the non-blocking button-state API described in
`docs/architecture.md`; they must not perform synchronous BLE work in interrupt
or system-workqueue context.

## Release power behavior

In the ESP32-C6 release build, no serial shell or logs is intentional. After the
idle timeout the BLE disconnect is an observable sign that sleep entry occurred,
not necessarily a fault. A 300 ms white RGB flash after a button wake is also
intentional; ordinary reset and power-up do not produce it.

Power measurements must be made with the OLED disconnected and USB removed,
using an analyzer in series with the intended supply path. DevKitC measurements
include board-level regulator and USB circuitry and must not be presented as a
bare-module or CR2032 lifetime measurement.
