# ESP32-C6 serial connectivity shell

An extensible ESP-IDF command shell for experimenting with the Wi-Fi,
Bluetooth LE, and system features of an ESP32-C6-DevKitC-1. This branch
intentionally has no display or joystick dependencies. Communication uses the
board's USB-to-UART bridge at 115200 baud.

The firmware uses ESP-IDF's `esp_console` REPL with line editing, command
completion, and argument splitting. History is kept to one in-memory entry;
credential-bearing `wifi join` commands immediately evict themselves and
history is never saved to flash.

## Build, upload, and connect

The Python and PlatformIO tools remain local to this repository:

```sh
make setup
make build
make upload
make serial
```

The configured serial device is `/dev/cu.usbserial-310`. `make serial` runs
`tools/c6_serial.py`, an interactive pyserial terminal with the correct port,
baud rate, control-line, and `CR` line-ending defaults. The `CR` setting avoids
submitting two empty commands for one Enter key, while received `CRLF` lines are
preserved to avoid double-spaced output. Press Enter if the `c6>` prompt is not
immediately visible. Exit with `Ctrl+]`.

Override the defaults when necessary:

```sh
make serial ARGS="/dev/cu.usbserial-OTHER 115200"
# or
C6_SERIAL_PORT=/dev/cu.usbserial-OTHER make serial
```

No external wiring is required. Displays and joysticks may remain disconnected.

## Commands

```text
help
sysinfo
heap
reboot
wifi help
wifi scan [limit]
wifi join <ssid> [password]
wifi status
wifi leave
camera help
camera pair [seconds]
camera connect
camera shutter
camera focus
camera status
camera disconnect
camera forget
```

Examples:

```text
c6> wifi scan 10
c6> wifi join "My Network" "correct horse battery staple"
c6> wifi status
c6> wifi leave
```

Quote values containing spaces. Credentials are passed to the ESP-IDF Wi-Fi
driver using `WIFI_STORAGE_RAM`; they are not compiled into the firmware or
written to NVS. `wifi leave` also clears the in-memory station configuration.
The password is still visible while typed in the terminal, so use the serial
shell only in a trusted local environment.

## Canon BLE remote

The `camera` service is a native ESP-IDF/NimBLE port of the protocol used by
[maxmacstn/ESP32-Canon-BLE-Remote](https://github.com/maxmacstn/ESP32-Canon-BLE-Remote).
It does not depend on Arduino, Arduino BLE, or ArduinoNvs. The saved camera
address and NimBLE bond survive a reboot in NVS.

To pair:

1. On the camera, open **Wireless Communication Settings > Bluetooth Function
   > Remote > Pairing**. The exact menu name varies by Canon model.
2. Run `camera pair 20` while that screen is open.
3. Accept or finish the pairing prompt on the camera if it asks.
4. Run `camera status`, then try `camera shutter` or `camera focus`.

After the initial pairing, control commands reconnect automatically when
needed. `camera disconnect` keeps the bond but closes the current connection;
`camera forget` removes both the saved address and the corresponding NimBLE
bond. If either side has stale pairing information, forget/delete the remote
on both devices and pair again.

The upstream project reports compatibility with the EOS M50. Canon models and
camera firmware can differ, so pairing and shutter behavior must still be
verified on the particular camera. For still photos, Canon may also require
the remote/self-timer drive mode; for movies, enable remote control in the
camera menu.

## Source layout

- `src/console_app.c`: UART REPL setup and command registration
- `src/system_commands.c`: chip, heap, uptime, and reboot commands
- `src/wifi_service.c`: Wi-Fi lifecycle, events, scanning, and station state
- `src/wifi_commands.c`: serial command parsing and formatted output
- `src/canon_ble_service.c`: Canon discovery, pairing, bonding, and controls
- `src/canon_ble_commands.c`: `camera` command parsing and status output
- `third_party/esp32-canon-ble-remote/`: upstream attribution and MIT license

New features should expose a small service API and register their shell
commands in a separate module. Long-lived state belongs in the service rather
than in a command handler.

## VS Code, clangd, and IntelliSense

The checked-in `.vscode` configuration points clangd and Microsoft C/C++
IntelliSense at PlatformIO's RISC-V compiler and compilation database. Generate
or refresh that database after changing build configuration:

```sh
make compile-commands
```

VS Code tasks are included for building, refreshing the database, and opening
the serial terminal. Both clangd and Microsoft C/C++ extensions are recommended;
enable only one diagnostics engine if duplicate diagnostics appear.

## Demo branches

```sh
git switch main       # serial Wi-Fi and Canon BLE command shell
git switch dual-rift  # dual-screen twin-stick Rift Runner
git switch arduboy    # Arduboy2 sprite port and playground
git switch renderer   # joystick-controlled 3D renderer
git switch ble        # BLE text display bridge
git switch games      # Snake + Flappy Bird console
```
