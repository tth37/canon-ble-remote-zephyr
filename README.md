# Multi-target wireless serial services

One repository now builds the serial command firmware for two microcontroller
targets:

| Target | SDK/backend | Available services | Validation |
|---|---|---|---|
| `esp32c6` | ESP-IDF 6 / NimBLE | system, Wi-Fi, Canon BLE remote | Build and on-board UART tested |
| `ch582m` | WCH CH58x SDK / TMOS BLE | system, Canon BLE remote | Build and link tested; hardware test pending |

The display and joystick demos remain on their existing branches. This branch
has no display or joystick dependency; both targets communicate over a
115200-baud serial shell.

## Select a target

The root Makefile reads the active target from the ignored `.target` file. If
that file is absent, the safe default is `esp32c6`.

```sh
make select TARGET=esp32c6
make build
make upload
make serial
```

Switch once, then keep using the ordinary commands:

```sh
make select TARGET=ch582m
make build
```

For CI or a one-off build, override the saved selection without changing it:

```sh
make build TARGET=esp32c6
make build TARGET=ch582m
```

`make target` prints the effective selection and `make help` lists the common
commands. Toolchains remain local to this repository under `.venv`,
`.platformio`, and `.cache`; none are installed into the system Python.

## Build and flash

For ESP32-C6, PlatformIO drives ESP-IDF:

```sh
make select TARGET=esp32c6
make setup
make build
make upload
```

The configured ESP32-C6 serial device is `/dev/cu.usbserial-310`. Override
automatic selection when necessary:

```sh
SERIAL_PORT=/dev/cu.usbserial-OTHER make serial
```

For CH582M, setup sparsely fetches a pinned revision of the official
`openwch/ch583` SDK, which also supports CH582M, and uses a repository-local
RISC-V compiler, CMake, and Ninja. Build outputs are written to
`.build/ch582m/` as ELF, Intel HEX, and raw binary files.

```sh
make select TARGET=ch582m
make setup
make build
```

CH582M flashing is intentionally left behind an explicit `wchisp` dependency
until the board is available:

```sh
make upload                       # uses wchisp from PATH
CH582M_FLASHER=/path/to/wchisp make upload
```

The CH582M shell uses UART1 at 115200 baud: PA9 is TX and PA8 is RX. The final
USB serial port and bootloader procedure must be verified with the actual
devkit.

## Serial commands

Both backends expose the same Canon command vocabulary:

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

`make serial` runs `tools/serial_terminal.py`. It transmits one carriage return
for Enter and preserves received CRLF, avoiding both duplicate prompts and
double-spaced output. Exit with `Ctrl+]`.

Wi-Fi credentials on ESP32-C6 use `WIFI_STORAGE_RAM`; they are not compiled
into the firmware or written to NVS. They remain visible while typed, so use
the serial shell only in a trusted environment.

## Canon BLE remote

The Canon service is a native port of the protocol used by
[maxmacstn/ESP32-Canon-BLE-Remote](https://github.com/maxmacstn/ESP32-Canon-BLE-Remote).
Neither target depends on Arduino. Shared C11 code owns the Canon UUIDs,
pairing/control packets, and stable saved-peer record; each firmware target
owns its BLE lifecycle, bond store, scheduler, and serial integration.

To pair:

1. On the camera, open **Wireless Communication Settings > Bluetooth Function
   > Remote > Pairing**. Exact menu names vary by model.
2. Run `camera pair 20` while that screen is open.
3. Finish any camera-side pairing prompt.
4. Run `camera status`, then `camera shutter` or `camera focus`.

The address record is portable, but vendor bond databases are not. Switching
physical boards therefore requires pairing that board once. `camera forget`
removes both its saved address and its backend-specific bond.

The original project reports EOS M50 compatibility. Camera model and firmware
behavior still need testing on the particular camera.

## Repository structure

```text
firmware/
  esp32c6/             ESP-IDF project and NimBLE/Wi-Fi services
  ch582m/              WCH CMake project, TMOS BLE service, UART shell
shared/canon/           vendor-neutral protocol and peer record code
tests/host/             native tests for shared code
third_party/            upstream attribution and SDK provenance
tools/                  target-neutral serial and SDK setup helpers
```

See [docs/architecture.md](docs/architecture.md) for the backend boundary and
steps for adding another MCU. Run the portable tests with `make test`.

## VS Code, clangd, and IntelliSense

Select the target, then generate that target's compilation database:

```sh
make compile-commands
```

The command points root `compile_commands.json` at the selected backend. The
checked-in VS Code configuration uses the repository-local RISC-V toolchain;
tasks are included for selection, build, upload, tests, database refresh, and
serial. Enable only clangd or Microsoft C/C++ diagnostics if duplicate messages
appear.

## Demo branches

```sh
git switch main       # tested ESP32-C6 serial services
git switch dual-rift  # dual-screen twin-stick Rift Runner
git switch arduboy    # Arduboy2 sprite port and playground
git switch renderer   # joystick-controlled 3D renderer
git switch ble        # BLE text display bridge
git switch games      # Snake + Flappy Bird console
```
