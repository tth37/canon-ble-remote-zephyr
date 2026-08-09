# ESP32-C6 BLE status display

Bluetooth LE transport proof for an ESP32-C6-DevKitC-1 and a 128x64 SSD1306
OLED. A macOS client connects to the board and writes text to a custom GATT
characteristic; the board displays that text.

This first phase deliberately uses an unauthenticated BLE connection rather
than bonding. It proves discovery, connection, GATT writes, and OLED updates
before pairing security or Codex status integration is added.

The completed Snake and Flappy Bird console is preserved on the `games` Git
branch.

## Wiring

| Display | ESP32-C6-DevKitC-1 |
| --- | --- |
| VCC | 3V3 |
| GND | G / GND |
| SDA | GPIO6 |
| SCL | GPIO7 |

The joystick is unused on the `main` branch and may remain connected.

## Protocol

- BLE device name: `Codex Display`
- Service UUID: `7b1e0001-6d8f-4b7a-9c2d-4a7f1d2e3c40`
- Writable/readable text UUID: `7b1e0002-6d8f-4b7a-9c2d-4a7f1d2e3c40`
- Maximum message: 120 UTF-8 bytes (ASCII renders best with the compact font)

No kernel driver is required on macOS. The Python client uses CoreBluetooth
through Bleak. On first use, macOS may ask for Bluetooth permission for the
terminal application running Python.

## Build and upload

```sh
make setup
make build
make upload
```

The OLED should progress through `STARTING BLE` and then `ADVERTISING / CODEX
DISPLAY`.

## Scan and send from macOS

Install the PC-side dependency into the same project-local `.venv`:

```sh
make ble-setup
```

Scan:

```sh
.venv/bin/python pc/ble_display.py scan
```

Send a test message:

```sh
.venv/bin/python pc/ble_display.py send "HELLO FROM MAC"
```

The client scans by device name, connects, writes the text, reads it back for
verification, and disconnects. The OLED returns to advertising after the
client disconnects without replacing the most recent message. That message
also remains available through the read characteristic.

## Git branches

```sh
git switch main   # BLE display bridge
git switch games  # Snake + Flappy Bird console
```

## Later Codex integration

Once this transport is reliable, a small local process can translate Codex
lifecycle/notification JSON into short messages such as `WORKING`, `WAITING`,
or `DONE`, then invoke this BLE client. That integration is intentionally not
part of the first connectivity milestone.
