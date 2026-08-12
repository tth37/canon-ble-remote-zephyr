# Zephyr Canon BLE remote

This project is a Zephyr-native Canon BLE remote. One
application and one source set build for both supported boards:

| Zephyr board target | Console | Validation |
|---|---|---|
| `esp32c6_devkitc/esp32c6/hpcore` | UART0, 115200 baud | Built, flashed, and tested with a Canon 200D II |
| `promicro_nrf52840/nrf52840/uf2` | native USB CDC | Built, flashed, and hardware-tested with the display and buttons |

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

The UART shell remains available with the OLED and physical controls attached;
it uses GPIO16/GPIO17 and does not share their pins. It is optional during
normal camera use and remains useful for diagnostics and recovery:

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
i2c scan i2c@60004000
```

The serial helper sends one carriage return for Enter and leaves received CRLF
untouched, preventing duplicate prompts and double-spaced output. It also
passes Zephyr's ANSI prompt and history-redraw sequences directly to the host
terminal, so arrow-key history and colored prompts render normally. Exit it
with `Ctrl+]`.

## ESP32-C6 display and buttons

The ESP32-C6 board overlay defines the following active hardware:

| Part | Module pin | ESP32-C6 pin |
|---|---|---|
| 128x64 SSD1306 OLED | VCC | 3V3 |
| 128x64 SSD1306 OLED | GND | GND |
| 128x64 SSD1306 OLED | SDA | GPIO6 |
| 128x64 SSD1306 OLED | SCL | GPIO7 |
| Focus button | one side | GPIO20 |
| Focus button | other side | GND |
| Shutter button | one side | GPIO21 |
| Shutter button | other side | GND |
| Recessed PAIR button | one side | GPIO19 |
| Recessed PAIR button | other side | GND |
| Onboard addressable RGB LED | data | GPIO8 |

The OLED is configured at I2C address `0x3c`. Power it from 3.3 V so any
module-mounted I2C pull-ups cannot expose the ESP32-C6 to 5 V. SPI2 is disabled
by the application overlay because Zephyr's upstream DevKitC definition also
assigns GPIO6/GPIO7 to that unused peripheral.

All three inputs use internal pull-ups and active-low edges. A board-specific
worker waits until an input has been quiet for 8 ms before reading its stable
state. Stable focus and shutter changes are then passed directly to the Canon
module's non-blocking state API, without a two-button chord delay. Pressing and
holding focus or shutter holds the corresponding camera-side state; releasing
the physical button releases it.

GPIO19 is a dedicated application PAIR input, not the board's EN/reset input.
To pair at any time, first put the camera in Bluetooth Remote pairing mode,
then hold the recessed PAIR button for five seconds. The OLED shows the hold
countdown and the subsequent 30-second scan. Focus and shutter are suppressed
while PAIR is held or pairing is active, and all buttons must be released
before camera control is armed again. Releasing PAIR early cancels the request.
Before camera registration starts, a new press of focus, shutter, or PAIR
cancels the operation; the PAIR release following the five-second hold does
not. Cancelling during that phase preserves the existing peer. Once a camera
has been found, its stale bond may need to be removed before forced re-pairing,
so a later registration failure can require another pairing attempt.

The onboard RGB LED needs no external wiring. It blinks blue at its normal
rate during the five-second PAIR hold, then blinks blue faster while pairing is
active. It shines green while focus is held and flashes red while shutter is
held. Pairing has the highest effect priority, followed by shutter and then
focus. The LED is off while the controls are idle.

## nRF52840 display and buttons

The Pro Micro/nice!nano-compatible nRF52840 uses the same display and controls
without a status-light indicator:

| Part | Board label | nRF52840 pin | Other connection |
|---|---|---|---|
| 128x64 SSD1306 OLED SDA | D6 | P1.00 | - |
| 128x64 SSD1306 OLED SCL | D7 | P0.11 | - |
| Focus button | D3 | P0.20 | GND |
| Shutter button | D4 | P0.22 | GND |
| Recessed PAIR button | D2 | P0.17 | GND |

Connect the OLED to the board's 3.3 V VCC and GND pins. The display uses I2C
address `0x3c`; all three buttons use internal pull-ups and active-low edges.
The nRF52840 overlay deliberately defines no `status-led` alias.

## Physical control reliability

GPIO interrupt handlers must call `canon_remote_set_button()` rather than the
synchronous `canon_remote_focus()` or `canon_remote_shutter()` pulse helpers.
The state API is non-blocking and ISR-safe: it only updates atomics and wakes a
dedicated Canon button thread. The board-specific GPIO adapter supplies the
quiet-time debounce layer described above.

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
button latency low and does not block the CPU, shell, OLED refresh, or GPIO
handling. The same path can be exercised from UART:

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
                         and Devicetree overlays
  src/
    canon/               complete Canon Remote module
    hardware/            GPIO controls and OLED status adapter
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
git switch main       # tested Zephyr BLE firmware baseline
git switch dual-rift  # dual-screen twin-stick Rift Runner
git switch arduboy    # Arduboy2 sprite port and playground
git switch renderer   # joystick-controlled 3D renderer
git switch ble        # BLE text display bridge
git switch games      # Snake and Flappy Bird
```
