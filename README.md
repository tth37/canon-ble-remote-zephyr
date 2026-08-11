# ESP32-C6 Canon BLE remote

This branch contains the camera-tested ESP32-C6 firmware. PlatformIO manages a
repository-local ESP-IDF toolchain; the application uses FreeRTOS, NimBLE, NVS,
and the ESP-IDF serial console. It has no display or joystick dependency.

## Build and flash

```sh
make setup
make build
make upload
make serial
```

Tooling is installed under `.venv` and `.platformio`, not globally. Override
automatic serial-port selection when necessary:

```sh
SERIAL_PORT=/dev/cu.usbserial-OTHER make serial
```

The terminal runs at 115200 baud. Press `Ctrl+]` to exit.

## Serial commands

```text
help
sysinfo
heap
reboot
camera help
camera pair [seconds]
camera connect
camera shutter
camera focus
camera status
camera disconnect
camera forget
wifi help
wifi scan [limit]
wifi join <ssid> [password]
wifi status
wifi leave
```

Wi-Fi credentials use RAM storage and are not compiled into the firmware or
written to NVS. They remain visible while typed, so use the serial shell only
in a trusted environment.

## Canon BLE remote

The Canon protocol is a native ESP-IDF port derived from
[maxmacstn/ESP32-Canon-BLE-Remote](https://github.com/maxmacstn/ESP32-Canon-BLE-Remote).
The upstream license and attribution are retained under
`third_party/esp32-canon-ble-remote`.

Pairing with the tested Canon EOS 200D II:

1. Open the camera's Bluetooth remote pairing screen.
2. Run `camera pair 20`.
3. Accept any camera-side prompt.
4. Run `camera status`, then `camera shutter` or `camera focus`.

The implementation intentionally starts BLE security as soon as the central
connection callback runs, discovers GATT only after encryption, writes the
Canon registration packet, waits for the bond to settle, disconnects, and
reconnects as a bonded remote. The PlatformIO pre-build script preserves this
ordering with the pinned ESP-IDF 6 NimBLE package.

## Development

```sh
make test
make compile-commands
```

`shared/canon` owns packet construction, UUID values, and the saved-peer record.
`firmware/esp32c6` owns the ESP-IDF/NimBLE lifecycle, storage, shell, and Wi-Fi
demo. See [docs/architecture.md](docs/architecture.md).

Other hardware and display experiments remain on their named branches. The
Zephyr-native multi-board rewrite is developed separately from this stable
branch.
