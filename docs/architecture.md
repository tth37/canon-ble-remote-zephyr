# Stable ESP32-C6 architecture

`main` is the known-good Canon EOS 200D II implementation. Its primary module
is `firmware/esp32c6/src/canon_ble_service.c`; callers use a small camera-command
interface while the implementation owns the full ESP-IDF/NimBLE connection,
security, discovery, bonding, persistence, and timing lifecycle.

The portable `shared/canon` module contains only behavior that is independent
of an RTOS or Bluetooth host:

- Canon service, pairing, and trigger UUID bytes
- pairing and button packet construction
- stable peer-address record encoding and validation

Its interface uses standard C types and is the test surface exercised by
`make test`. ESP-IDF types do not cross that seam.

Platform-specific policy stays in `firmware/esp32c6`:

- FreeRTOS synchronization and delays
- NimBLE central callbacks and security
- NVS address and bond persistence
- ESP console commands
- Wi-Fi demonstration commands

The Zephyr rewrite lives on a separate branch until it passes the same physical
camera tests. Keeping the proven module intact provides a behavioral reference
for connection parameters and the security-before-GATT ordering.
