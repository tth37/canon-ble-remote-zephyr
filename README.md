# ESP32-C6 joystick 3D renderer

A tiny software 3D renderer for an ESP32-C6-DevKitC-1 and a 128x64 SSD1306
OLED. It perspective-projects a wireframe cube and uses the analog joystick to
orbit the camera.

## Wiring

Connect wires while USB power is disconnected.

| Display | ESP32-C6-DevKitC-1 |
| --- | --- |
| VCC | 3V3 |
| GND | G / GND |
| SDA | GPIO6 |
| SCL | GPIO7 |

| Joystick | ESP32-C6-DevKitC-1 |
| --- | --- |
| GND | G / GND |
| +5V | **3V3 (not 5V)** |
| VRx | GPIO0 / ADC1_CH0 |
| VRy | GPIO1 / ADC1_CH1 |
| SW | GPIO2 |

## Controls

Leave the joystick centered during the brief startup calibration.

- VRx: orbit left/right
- VRy: tilt up/down
- SW: reset to the initial camera angle

Farther cube edges are dotted and nearer edges are solid to improve depth
perception on the monochrome display. If an axis is physically reversed,
change `JOYSTICK_INVERT_X` or `JOYSTICK_INVERT_Y` in
`src/display_config.h`.

## Build and upload

```sh
make setup
make build
make upload
```

## Git branches

```sh
git switch main   # joystick-controlled 3D renderer
git switch ble    # verified BLE text display bridge
git switch games  # Snake + Flappy Bird console
```
