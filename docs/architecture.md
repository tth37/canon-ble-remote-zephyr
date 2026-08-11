# Multi-target architecture

## Boundary

This repository uses compile-time target selection, not a runtime hardware
abstraction layer. ESP-IDF and Zephyr have different schedulers, storage APIs,
BLE hosts, and asynchronous lifecycle rules. Hiding those differences behind
a lowest-common-denominator BLE interface would make the critical connection
sequence less visible and harder to test.

Code is shared only where behavior is genuinely platform-independent:

- Canon service, pairing, and trigger UUID bytes
- pairing and button packet construction
- stable peer-address record encoding and validation
- native host tests for those rules

Target projects own hardware and SDK policy:

- `firmware/esp32c6` owns ESP-IDF tasks, NimBLE, NVS, Wi-Fi, and
  `esp_console`.
- `firmware/nrf52840` owns Zephyr Bluetooth callbacks, settings/NVS, kernel
  synchronization, the shell, native USB CDC, and UF2 output.

The shared directory must not include ESP-IDF or Zephyr headers. Its public
types use only standard C headers, and `make test` compiles it with the host
compiler.

## Build selection

The root Makefile is the stable interface. `make select TARGET=<name>` writes
the developer-local `.target`; ordinary goals then dispatch to that firmware
project. `TARGET=<name>` on an individual invocation is the stateless override
for CI and scripts.

Each target remains independently buildable and owns its generated output:

- ESP32-C6: `firmware/esp32c6/.pio`
- Pro Micro nRF52840: `firmware/nrf52840/build`

The nRF target uses a local West workspace rooted at `firmware/`; its pinned
Zephyr checkout and imported modules live under the ignored
`firmware/.platformio/zephyr-workspace`. This keeps SDK state outside the
application directory while still making setup reproducible.

PlatformIO still supplies the repository-local ARM compiler, CMake, and Ninja.
Its Nordic platform currently pins Zephyr 2.7.1 and does not provide the
official Pro Micro board target, so West owns Zephyr 4.2 and its module graph.
This avoids maintaining a custom PlatformIO platform or a backported board
definition.

Root `compile_commands.json` is a generated symlink to the selected target's
database, so editor configuration does not encode the active MCU.

## Canon BLE connection sequencing

Canon remote connections require security as soon as the physical link is
established. Neither backend may begin service or characteristic discovery on
an unencrypted link.

Initial pairing is a two-link transaction:

1. Connect and initiate BLE security from the connection callback.
2. After encryption succeeds, discover and write the Canon registration
   characteristic.
3. Save the identity address, allow the bond to settle, disconnect, and wait.
4. Reconnect using the saved bond, restore encryption immediately, then
   discover the trigger characteristic.

The ESP32-C6 adapter enforces this with NimBLE GAP events and
`ble_gap_security_initiate`. The nRF52840 adapter enforces it with Zephyr
connection/security callbacks, semaphores for command-side waits, and
`bt_conn_set_security`. Both use the same proven scan and connection intervals
and the same registration/button timing.

## Adding another target

1. Add its name to `SUPPORTED_TARGETS` in the root Makefile.
2. Create `firmware/<target>` with its SDK entry point and build files.
3. Implement serial, storage, and the Canon BLE lifecycle using native SDK
   mechanisms.
4. Link `shared/canon/src/canon_protocol.c`; do not copy protocol constants.
5. Add setup, build, upload, serial, clean, and compilation-database dispatch.
6. Build with strict warnings, then validate boot, serial line endings,
   pairing, bonded reconnect, shutter, focus, disconnect, and forget on real
   hardware.

Feature parity stays explicit: ESP32-C6 exposes Wi-Fi commands, while nRF52840
does not because it has no Wi-Fi radio.
