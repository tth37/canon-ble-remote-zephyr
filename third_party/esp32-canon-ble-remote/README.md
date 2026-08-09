# ESP32 Canon BLE Remote attribution

The native ESP-IDF implementation in `src/canon_ble_service.c` is a clean C
port of the protocol behavior from
[maxmacstn/ESP32-Canon-BLE-Remote](https://github.com/maxmacstn/ESP32-Canon-BLE-Remote),
upstream commit `ba4fe82fa59c596a78cff25f5794c6babf6d8bd1`.

The upstream Arduino and ArduinoNvs sources are intentionally not vendored.
The port uses ESP-IDF NimBLE and NVS directly. See `LICENSE.md` for the
upstream MIT license.
