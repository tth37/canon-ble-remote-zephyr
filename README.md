# Zephyr Canon BLE remote

This branch is a Zephyr-native rewrite of the Canon BLE remote. One
application and one source set build for both supported boards:

| Zephyr board target | Console | Validation |
|---|---|---|
| `esp32c6_devkitc/esp32c6/hpcore` | UART0, 115200 baud | Built, flashed, and tested with a Canon 200D II |
| `promicro_nrf52840/nrf52840/uf2` | native USB CDC | Builds and produces UF2; hardware validation pending |

The proven PlatformIO/ESP-IDF/NimBLE ESP32-C6 implementation remains on the
`main` branch. This branch intentionally has no PlatformIO project, Wi-Fi
commands, display code, or joystick code.

## Reproducible setup

A fresh checkout needs only
[`uv`](https://docs.astral.sh/uv/getting-started/installation/) on `PATH`:

```sh
make setup
```

Everything else is installed under ignored repository-local directories:

- `requirements-tools.txt` pins the bootstrap tools installed in `.venv`:
  West, CMake, Ninja, and pyserial.
- `firmware/west.yml` pins Zephyr 4.2 and its allowlisted modules under
  `.zephyr`.
- `west packages pip --install` installs the Python dependencies declared by
  that exact Zephyr checkout into the same `.venv`.
- `west sdk install` installs Zephyr SDK 0.17.2 and only the ARM and RISC-V
  toolchains needed here under `.zephyr-sdk`.
- `west blobs fetch hal_espressif` installs the ESP32-C6 radio firmware blobs.

Do not install any of those globally. Re-running `make setup` is incremental.
If `requirements-tools.txt` or `firmware/west.yml` changes, Make refreshes the
corresponding local dependencies.

## Select, build, and flash

ESP32-C6 is the default. The selected native Zephyr board is stored in the
ignored `.board` file:

```sh
make select BOARD=esp32c6_devkitc/esp32c6/hpcore
make build
make upload
make serial
```

Select the Pro Micro nRF52840 with:

```sh
make select BOARD=promicro_nrf52840/nrf52840/uf2
make build
make upload
make serial
```

`BOARD=...` can be passed directly to any command for a one-off or CI build
without changing `.board`. Build output is isolated per board under `.build`.
`FLASH_ARGS` forwards additional arguments to `west flash`, and `SERIAL_PORT`
overrides automatic serial-port detection.

Useful commands:

```sh
make help
make board
make compile-commands
make pristine
```

The corresponding direct Zephyr command is also available after setup:

```sh
.venv/bin/west build -b esp32c6_devkitc/esp32c6/hpcore -d \
  .build/esp32c6_devkitc_esp32c6_hpcore firmware
```

## Serial shell

The firmware exposes only system and Canon remote commands:

```text
help
sysinfo
reboot
camera help
camera pair [seconds]
camera connect
camera shutter [press|release]
camera focus [press|release]
camera status
camera disconnect
camera forget
```

The serial helper sends one carriage return for Enter and leaves received CRLF
untouched, preventing duplicate prompts and double-spaced output. It also
passes Zephyr's ANSI prompt and history-redraw sequences directly to the host
terminal, so arrow-key history and colored prompts render normally. Exit it
with `Ctrl+]`.

## Physical button integration

GPIO interrupt handlers must call `canon_remote_set_button()` rather than the
synchronous `canon_remote_focus()` or `canon_remote_shutter()` pulse helpers.
The state API is non-blocking and ISR-safe: it only updates atomics and wakes a
dedicated Canon button thread. Debouncing remains the responsibility of the
board-specific GPIO layer.

```c
canon_remote_set_button(CANON_REMOTE_BUTTON_FOCUS, true);   /* pressed */
canon_remote_set_button(CANON_REMOTE_BUTTON_FOCUS, false);  /* released */
```

The worker has a six-second total setup deadline. If every physical button is
released while connection, security, or discovery is pending, it notices
within approximately 20 ms and cancels the BLE procedure. It never reconnects
solely to deliver a release: a lost BLE link already releases the camera-side
state. Any failed state write forces a disconnect instead of leaving an
uncertain pressed state.

An established encrypted link normally stays idle between presses. That keeps
button latency low and does not block the CPU, shell, or GPIO handling. Use the
following commands to exercise the future physical-button path before GPIOs
are added:

```text
camera focus press
camera focus release
camera shutter press
camera shutter release
```

## Pair a Canon camera

1. Open the camera's Bluetooth Remote pairing screen.
2. Run `camera pair 20`.
3. Accept any camera-side prompt and let the firmware perform its bonded
   reconnect.
4. Check `camera status`, then try `camera focus` or `camera shutter`.

Canon connections must be encrypted before GATT discovery. The implementation
starts security from the connection callback, then performs Canon service and
characteristic discovery only after security succeeds. Initial registration
uses one link, persists the peer, lets bonding settle, disconnects, and makes a
second encrypted connection for camera control.

`camera forget` removes both the portable peer record and Zephyr's bond. Bond
databases are board-local, so each physical board must pair once.

The ESP32-C6 hardware test covers initial pairing, encrypted control setup,
bond persistence across resets, power-cycle reconnect, focus, shutter,
explicit disconnect, and reconnect. The working bond was deliberately kept;
`camera forget` is exercised only when the camera is meant to be paired again.

Zephyr's ESP simple-boot image uses a RAM-only ROM header and intentionally has
no appended SHA-256 digest because its flash-mapped segments follow the RAM
image in the same binary. Early ESP32-C6 ROM revisions may therefore print
`SHA-256 comparison failed` followed by `Attempting to boot anyway`. This is an
expected simple-boot diagnostic; West still verifies the complete image while
flashing, and Zephyr performs the remaining image setup after ROM handoff.

The Canon protocol is a native C port of
[maxmacstn/ESP32-Canon-BLE-Remote](https://github.com/maxmacstn/ESP32-Canon-BLE-Remote).
Attribution and its license are retained under `third_party`.

## Repository structure

```text
firmware/                one Zephyr application for every board
  boards/                board-specific Kconfig fragments
  src/
    canon/               complete Canon Remote module
    main.c               application entry point
    shell_commands.c     serial-shell adapter
  west.yml               pinned Zephyr workspace manifest
third_party/             upstream attribution and license
tools/                   interactive serial helper
```

See [docs/architecture.md](docs/architecture.md) for the portability boundary.

## VS Code and clangd

Generate the active board's compilation database after selecting it:

```sh
make compile-commands
```

The checked-in VS Code and clangd settings query both local Zephyr SDK
compilers. Since the root `compile_commands.json` points at the active board,
editor diagnostics follow the same board selection as the build.

## Other branches

```sh
git switch main       # proven ESP32-C6 ESP-IDF/NimBLE implementation
git switch dual-rift  # dual-screen twin-stick Rift Runner
git switch arduboy    # Arduboy2 sprite port and playground
git switch renderer   # joystick-controlled 3D renderer
git switch ble        # BLE text display bridge
git switch games      # Snake and Flappy Bird
```
