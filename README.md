# Zephyr Canon BLE remote

This project is a Zephyr-native Canon BLE remote. One
application and one source set build for both supported boards:

| Zephyr board target | Console | Validation |
|---|---|---|
| `esp32c6_devkitc/esp32c6/hpcore` | UART0 in debug builds | Built, flashed, and tested with a Canon 200D II |
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

ESP32-C6 with the debug profile is the default. Use the project configuration
menu to select a board and profile; the result is stored in the ignored root
`.config` file:

```sh
make menuconfig
make build
make upload
make serial
```

The menu offers ESP32-C6 debug, ESP32-C6 release, and Pro Micro nRF52840 debug.
Release is ESP32-C6-only because its deep-sleep policy uses Espressif wake
hardware. Inspect the saved selection without opening the menu with:

```sh
make selection
```

`BOARD=...` and `PROFILE=...` can still be passed directly to any command for
a one-off or CI build without changing `.config`. Build output is isolated per
board and profile under `.build`.
`FLASH_ARGS` forwards additional arguments to `west flash`, and `SERIAL_PORT`
overrides automatic serial-port detection.

### ESP32-C6 build profiles

Both ESP32-C6 profiles use the same screenless hardware: three ordinary
buttons and an external common-cathode RGB LED. Debug is the default and keeps
the UART shell and logging available through `firmware/prj_debug.conf`. Release
uses `firmware/prj_release.conf` and is the product firmware:

```sh
make build BOARD=esp32c6_devkitc/esp32c6/hpcore PROFILE=debug
make build BOARD=esp32c6_devkitc/esp32c6/hpcore PROFILE=release
make upload BOARD=esp32c6_devkitc/esp32c6/hpcore PROFILE=release
```

The release profile disables the shell, logging, and console. After 30 seconds
without a button edge or active BLE operation, it disconnects, turns off the
RGB LED, and enters deep sleep. Once asleep, any button wakes the chip through
a reset; a held focus or shutter input is forwarded after boot so the waking
press is not discarded. A GPIO deep-sleep wake produces a 300 ms white RGB flash before BLE
initialization; ordinary reset and power-up do not. The bond and saved camera
identity remain in flash. The debug profile stays awake for diagnostics.

ESP32-C6 deep-sleep wake is limited to RTC-capable GPIO0 through GPIO7, so this
shared wiring keeps every button in that range:

| Control | ESP32-C6 pin | Other connection |
|---|---|---|
| Focus button | GPIO6 | GND |
| Shutter button | GPIO7 | GND |
| Recessed PAIR button | GPIO4 | GND |
| RGB LED red leg | GPIO0 through 1 kΩ | - |
| RGB LED green leg | GPIO1 through 1 kΩ | - |
| RGB LED blue leg | GPIO2 through 1 kΩ | - |
| RGB LED common cathode | - | GND |

Use one resistor per color channel; do not connect a color leg directly to a
GPIO. All inputs are active-low with internal pull-ups. Do not attach the old
OLED wiring because GPIO6 and GPIO7 are now focus and shutter in both profiles.

For power measurements, supply the board through its power input with USB
disconnected and place a power analyzer in series. Record at least these
states separately:

1. Deep sleep after the 30-second timeout.
2. Awake and connected to the camera with no button held.
3. Wake, reconnect, focus, and shutter energy over a representative session.

The DevKitC includes its own regulator, USB circuitry, and board-level loads,
so its result predicts this development board, not a future bare ESP32-C6 PCB.
Estimate a CR2032 only from the measured time-weighted average current:

```text
average current = total measured charge / measurement duration
ideal life hours = usable battery capacity in mAh / average current in mA
```

Use the battery's usable capacity at the measured pulse load and cutoff
voltage, then apply margin for temperature and self-discharge. A CR2032 should
not be connected directly to the DevKitC until its required supply path and
radio-current peaks have been verified.

Useful commands:

```sh
make help
make board
make selection
make menuconfig
make zephyr-menuconfig
make compile-commands
make pristine
```

`make menuconfig` selects the project board and profile. After a build exists,
`make zephyr-menuconfig` opens Zephyr's full Kconfig interface for that selected
firmware build; it is intended for inspection and temporary experiments rather
than defining a committed profile.

The corresponding direct Zephyr command is also available after setup:

```sh
.venv/bin/west build -b esp32c6_devkitc/esp32c6/hpcore -d \
  .build/esp32c6_devkitc_esp32c6_hpcore_debug firmware -- \
  -DFILE_SUFFIX=debug
```

## Serial shell

The UART shell is available only in `PROFILE=debug`. It uses GPIO16/GPIO17 and
does not share the controls or indicator pins:

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

## ESP32-C6 controls and indicator

The ESP32-C6 board overlay defines one hardware layout shared by debug and
release builds:

| Part | Module pin | ESP32-C6 pin |
|---|---|---|
| Focus button | one side | GPIO6 |
| Focus button | other side | GND |
| Shutter button | one side | GPIO7 |
| Shutter button | other side | GND |
| Recessed PAIR button | one side | GPIO4 |
| Recessed PAIR button | other side | GND |
| RGB LED red leg | through 1 kΩ | GPIO0 |
| RGB LED green leg | through 1 kΩ | GPIO1 |
| RGB LED blue leg | through 1 kΩ | GPIO2 |
| RGB LED common cathode | GND | - |

SPI2 is disabled by the application overlay because Zephyr's upstream DevKitC
definition assigns GPIO2/GPIO6/GPIO7 to that unused peripheral.

All three inputs use internal pull-ups and active-low edges. A board-specific
worker waits until an input has been quiet for 8 ms before reading its stable
state. Stable focus and shutter changes are then passed directly to the Canon
module's non-blocking state API, without a two-button chord delay. Pressing and
holding focus or shutter holds the corresponding camera-side state; releasing
the physical button releases it.

GPIO4 is a dedicated application PAIR input, not the board's EN/reset input.
To pair at any time, first put the camera in Bluetooth Remote pairing mode,
then hold the recessed PAIR button for five seconds. The LED blinks blue during
the hold and faster during the subsequent scan. Focus and shutter are
suppressed while PAIR is held or pairing is active, and all buttons must be
released before camera control is armed again. Releasing PAIR early cancels the
request.
Before camera registration starts, a new press of focus, shutter, or PAIR
cancels the operation; the PAIR release following the five-second hold does
not. Cancelling during that phase preserves the existing peer. Once a camera
has been found, its stale bond may need to be removed before forced re-pairing,
so a later registration failure can require another pairing attempt.

Use one resistor per color channel; do not connect a color leg directly to a
GPIO. The LED blinks blue at its normal rate during the five-second PAIR hold,
then blinks blue faster while pairing is active. It shines green while focus is
held and flashes red while shutter is held. Pairing has the highest effect
priority, followed by shutter and then focus. The LED is off while the controls
are idle.

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
button latency low and does not block the CPU, shell, indicator, or GPIO
handling. The same path can be exercised from UART in a debug build:

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
    hardware/            controls, indicator, display, and power adapters
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
