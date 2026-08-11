# Multi-target Canon BLE remote

This repository builds the same Canon BLE remote and serial command interface
for two targets:

| Target | SDK/backend | Available services | Validation |
|---|---|---|---|
| `esp32c6` | ESP-IDF 6 / NimBLE | system, Wi-Fi, Canon BLE remote | Built and tested on hardware with a Canon 200D II |
| `nrf52840` | Zephyr 4.2 / native Bluetooth host | system, Canon BLE remote | Build and UF2 generation tested; hardware test pending |

The display and joystick demos remain on their existing branches. This branch
has no display or joystick dependency. The ESP32-C6 uses its USB-to-UART
bridge; the Pro Micro nRF52840 uses native USB CDC.

## Select a target

The root Makefile reads the active target from the ignored `.target` file. If
the file is absent, the default is `esp32c6`.

```sh
make select TARGET=esp32c6
make build
make upload
make serial
```

Switch once, then continue using the same ordinary commands:

```sh
make select TARGET=nrf52840
make build
```

For CI or a one-off build, override the saved selection without changing it:

```sh
make build TARGET=esp32c6
make build TARGET=nrf52840
```

`make target` prints the effective selection. `make help` lists the common
commands. Python packages, SDKs, and compilers stay inside `.venv`,
`.platformio`, and ignored firmware workspace directories.

## Build and flash ESP32-C6

PlatformIO drives ESP-IDF:

```sh
make select TARGET=esp32c6
make setup
make build
make upload
make serial
```

The preferred ESP32-C6 serial device is `/dev/cu.usbserial-310`. Override
automatic selection when needed:

```sh
SERIAL_PORT=/dev/cu.usbserial-OTHER make serial
```

## Build and flash Pro Micro nRF52840

The nRF backend uses the official Zephyr 4.2
[`promicro_nrf52840/nrf52840/uf2`](https://docs.zephyrproject.org/latest/boards/others/promicro_nrf52840/doc/index.html)
board target. West fetches only the allowlisted Zephyr modules, while the ARM
compiler, CMake, Ninja, West, and Python dependencies remain repository-local.

```sh
make select TARGET=nrf52840
make setup                 # first run downloads the pinned SDK and tools
make build                 # creates firmware/nrf52840/build/zephyr/zephyr.uf2
```

To flash, double-tap the board's reset pin/button so its UF2 drive mounts, then
run:

```sh
make upload
make serial
```

If more than one UF2 drive is mounted, select it explicitly:

```sh
UF2_VOLUME=/Volumes/NICENANO make upload
```

`make serial` automatically looks for the board's native USB CDC port. You can
override it with `SERIAL_PORT` on any platform.

## Serial commands

Both backends expose the same Canon commands:

```text
help
sysinfo
reboot
camera help
camera pair [seconds]
camera connect
camera shutter
camera focus
camera status
camera disconnect
camera forget
```

ESP32-C6 additionally provides:

```text
heap
wifi help
wifi scan [limit]
wifi join <ssid> [password]
wifi status
wifi leave
```

The serial helper transmits one carriage return for Enter and preserves
received CRLF, avoiding duplicate prompts and double-spaced output. Exit with
`Ctrl+]`.

Wi-Fi credentials on ESP32-C6 use `WIFI_STORAGE_RAM`; they are not compiled
into the firmware or written to NVS. They remain visible while typed, so use
the serial shell only in a trusted environment.

## Canon BLE remote

The protocol is a native C port of
[maxmacstn/ESP32-Canon-BLE-Remote](https://github.com/maxmacstn/ESP32-Canon-BLE-Remote).
Neither target depends on Arduino. Shared C11 code owns the Canon UUIDs,
pairing/control packets, and stable peer record. Each firmware target owns its
BLE lifecycle, bond database, scheduling, storage, and serial integration.

To pair:

1. On the camera, open **Wireless Communication Settings > Bluetooth Function
   > Remote > Pairing**. Exact menu names vary by model.
2. Run `camera pair 20` while that screen is open.
3. Finish any camera-side pairing prompt.
4. Run `camera status`, then `camera shutter` or `camera focus`.

The address record format is portable, but vendor bond databases are not.
Switching physical boards requires pairing that board once. `camera forget`
removes its saved address and backend-specific bond.

Canon remote connections need encryption immediately after the physical BLE
link is established. Both backends therefore start security in the connection
callback and postpone GATT discovery until encryption succeeds. Initial
registration writes the pairing characteristic, allows the bond to settle,
disconnects, waits, then reconnects using the saved bond before enabling
camera controls.

ESP-IDF 6's NimBLE fork delays central connection callbacks until optional
remote capability queries finish. Some Canon cameras disconnect before that
delay expires. The ESP32-C6 pre-build script applies a checked local patch that
restores the upstream Apache NimBLE callback order and fails safely on an
unknown framework version.

## Repository structure

```text
firmware/
  esp32c6/             ESP-IDF/NimBLE project and Wi-Fi services
  nrf52840/            Zephyr project, native BLE adapter, USB CDC shell
shared/canon/           vendor-neutral protocol and peer record code
tests/host/             native tests for shared code
third_party/            upstream Canon remote attribution and license
tools/                  serial terminal and UF2 upload helpers
```

See [docs/architecture.md](docs/architecture.md) for the backend boundary and
steps for adding another MCU. Run the portable tests with `make test`.

## VS Code, clangd, and IntelliSense

Select a target, then generate its compilation database:

```sh
make compile-commands
```

The command points root `compile_commands.json` at the selected backend. The
checked-in editor settings recognize both repository-local cross-compilers.
Tasks are included for target selection, build, upload, tests, database
refresh, and serial. Enable only clangd or Microsoft C/C++ diagnostics if
duplicate messages appear.

## Demo branches

```sh
git switch main       # tested ESP32-C6 serial services
git switch dual-rift  # dual-screen twin-stick Rift Runner
git switch arduboy    # Arduboy2 sprite port and playground
git switch renderer   # joystick-controlled 3D renderer
git switch ble        # BLE text display bridge
git switch games      # Snake + Flappy Bird console
```
