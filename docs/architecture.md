# Multi-target architecture

## Boundary

This repository uses compile-time target selection, not a runtime hardware
abstraction layer. The two MCUs have different SDKs, schedulers, storage APIs,
and BLE host stacks, so pretending those mechanisms are identical would make
the shared interface harder to understand and test.

Code is shared only where the behavior is genuinely platform-independent:

- Canon service, pairing, and trigger UUID bytes
- pairing and button packet construction
- stable peer-address record encoding and validation
- native host tests for those rules

Target projects own hardware and SDK policy:

- `firmware/esp32c6` uses ESP-IDF tasks, NimBLE, NVS, and `esp_console`.
- `firmware/ch582m` uses TMOS events, the WCH central-role stack, DataFlash,
  and a polling UART1 console.

The shared directory must not include ESP-IDF or WCH headers. Its public types
use only standard C headers, and `make test` compiles it with the host compiler.

## Build selection

The root Makefile is the stable interface. `make select TARGET=<name>` writes
the developer-local `.target`; every ordinary goal then dispatches to the
selected firmware project. `TARGET=<name>` on an individual invocation is the
stateless override used by CI.

Each target remains independently buildable and owns its generated output:

- ESP32-C6: `firmware/esp32c6/.pio`
- CH582M: `.build/ch582m`

The root `compile_commands.json` is a generated symlink to the currently
selected target's database so editor configuration does not need to change.

## Adding another target

1. Add its name to `SUPPORTED_TARGETS` in the root Makefile.
2. Create `firmware/<target>` with its own SDK entry point and build files.
3. Implement the serial and BLE lifecycle with that SDK's native model.
4. Link `shared/canon/src/canon_protocol.c`; do not copy protocol constants.
5. Add setup, build, upload, serial, clean, and compilation-database dispatch.
6. First compile with warnings treated as errors, then validate boot, serial
   line endings, status commands, pairing, reconnect, shutter, focus, and
   forgetting on physical hardware.

Feature parity is explicit rather than artificial: for example, CH582M does
not expose Wi-Fi commands because it has no Wi-Fi radio.
