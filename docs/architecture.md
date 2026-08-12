# Zephyr-native architecture

## One application, multiple boards

The firmware is one Zephyr application, not a dispatcher containing separate
ESP32-C6 and nRF52840 projects. The same sources are compiled for both native
Zephyr board targets. The root Makefile is a convenience interface around
West; it does not model another framework or invent project-specific target
names.

Board variation belongs in Zephyr's existing seams:

- Devicetree describes peripherals, pins, flash, and console devices.
- `firmware/boards/<board>.conf` selects board-specific Kconfig policy.
- Zephyr supplies the Bluetooth controller, host, storage, kernel, shell, and
  flash APIs used by the application.

There is deliberately no custom HAL or application-level `if (esp32)` /
`if (nrf52)` switch. A new Zephyr-supported board should need a configuration
fragment, not a copied firmware backend.

## Canon Remote module

`firmware/src/canon` is one cohesive Canon Remote module. `remote.h` is its
only public interface. Behind that interface, the implementation owns:

- Canon UUIDs, registration packets, and trigger packets
- the versioned, checksummed peer-address record
- BLE scan, connection, security, and GATT discovery
- bond/settings operations and command synchronization

`protocol_internal.h` is private to the module; callers do not need to know
the Canon wire format or storage encoding. These operations are tightly
coupled by Canon's protocol ordering, so exposing separate generic scanning,
storage, packet, and GATT interfaces would obscure the lifecycle without
creating useful seams.

The shell is only an adapter: it parses user input, calls the Canon Remote
module, and formats status. It does not own BLE state.

`firmware/src/hardware` is another adapter. Its GPIO worker applies an 8 ms
quiet-time debounce before calling `canon_remote_set_button()` for stable focus
and shutter transitions. A separate recessed PAIR input invokes the module's
pairing transaction after a five-second hold, suppressing the camera controls
until pairing finishes and all inputs are released. This keeps pairing
recognition out of the latency-sensitive focus and shutter paths. The OLED
worker renders the module's thread-safe status snapshot, physical inputs, and
PAIR countdown. Board-specific Devicetree overlays supply the concrete pins and
SSD1306 node. Targets without those aliases and the chosen display compile
small no-op adapters, keeping board selection out of the Canon module.

While the synchronous pairing transaction owns the GPIO worker, a debounced
delayable work item watches for fresh button presses and advances the Canon
module's cancellation generation. Guarded scan, connection, security, and
discovery waits notice cancellation within approximately 20 ms. Cancellation
closes before the registration write, after which bonding and reconnect finish
as one transaction. The release of the original PAIR hold is ignored, while a
later press during the cancellable phase does not leak a camera command.

The RGB indicator worker reads the same snapshots and drives the ESP32-C6
DevKitC's single WS2812-compatible LED through Zephyr's I2S LED-strip driver.
It is independent of GPIO and BLE workers: pairing produces a blue blink,
with a faster rate after the five-second hold; shutter produces a red flash,
focus steady green, and idle turns the LED off. Targets without the
`status-led` alias compile a no-op indicator adapter.

The UART shell remains active alongside the hardware adapter. It is an
independent control and diagnostic surface, not a runtime dependency of the
buttons or display.

Physical button edges use the module's non-blocking state API. Atomic desired
state is consumed by a dedicated worker thread, keeping all connection,
security, discovery, and GATT waits out of GPIO interrupt and system-workqueue
context. A release-to-idle transition cancels in-flight setup; connection
callbacks and the worker coordinate through synchronized connection ownership
and bounded waits. Maintenance commands are rejected as busy while a physical
button is requested or applied so they cannot delay a required release.

## Canon connection lifecycle

Canon remote control requires an encrypted link before GATT discovery. Initial
pairing is a two-link transaction:

1. Scan for the Canon service and connect to the discovered identity.
2. Start BLE security immediately from the connection callback.
3. After encryption succeeds, discover and write the Canon registration
   characteristic.
4. Persist the identity address, allow the bond to settle, and disconnect.
5. Reconnect with the saved bond, restore encryption immediately, and discover
   the trigger characteristic.
6. Mark controls ready only after the second link completes discovery.

Ordinary `camera connect` begins at step 5. `camera forget` deletes both the
portable peer record and Zephyr's Bluetooth keys.

The portable peer record can move between implementations, but Zephyr bond
data belongs to the board's settings store. Copying a peer address does not
copy its encryption keys.

## Toolchain ownership

Dependency ownership is split by responsibility:

- `requirements-tools.txt` pins the few Python/bootstrap executables this
  repository invokes directly.
- `firmware/west.yml` pins Zephyr and the module graph.
- The selected Zephyr revision declares its own Python package requirements.
- West installs the matching Zephyr SDK toolchains and Espressif blobs.

This is preferable to duplicating Zephyr's evolving transitive Python
requirements in this repository. A clean `make setup` reconstructs the local
workspace without global compiler or Python package installs.

The only supported application build path is `west build`. The Makefile adds
selection persistence, setup markers, per-board build directories, and editor
integration; it does not invoke PlatformIO.

## Adding another Zephyr board

1. Confirm the upstream Zephyr board target supports a serial console,
   persistent settings, BLE central/GATT client, and SMP bonding.
2. Add its exact board target to `SUPPORTED_BOARDS` in the root Makefile.
3. Add a board Kconfig fragment only if the common `prj.conf` is insufficient.
4. Add its compiler architecture to the `west sdk install` toolchain list if
   ARM or RISC-V does not cover it.
5. Build with strict warnings, then test boot, serial input, pairing, bonded
   reconnect, focus, shutter, disconnect, forget, and power-cycle reconnect on
   real hardware.

If a future target is not supported by Zephyr, keep it as a separate project
rather than weakening this application's boundary with a pseudo-Zephyr HAL.
