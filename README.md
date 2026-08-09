# Arduboy2 graphics on ESP32-C6

An ESP-IDF adaptation of the useful Arduboy2 graphics/game layer for an
ESP32-C6-DevKitC-1, analog joystick, and 128x64 SSD1306 OLED.

The demo is a tiny animated space playground: steer the robot into energy
crystals to increase the score. Press the joystick switch to warp somewhere
else. It exercises Arduboy-style plus-mask sprites, animation frames, collision
detection, and fixed frame timing over the existing ESP-IDF display driver.

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

Leave the joystick centered during startup calibration.

## Ported API

`src/arduboy2_port.c` adapts Arduboy2's portable graphics concepts directly
to the SSD1306's 1 KB page-organized framebuffer:

- fixed-rate `nextFrame` and `everyXFrames` timing
- raw Arduboy-format bitmaps
- overwrite, self-masked, erase, external-mask, and plus-mask sprites
- multiple animation frames per sprite
- rectangle collision detection

The upstream revision and applicable licenses are recorded in
`third_party/arduboy2/LICENSE.txt`. Arduino, AVR assembly, audio, EEPROM, USB,
and the original hardware layer are intentionally not included.

## Build and upload

```sh
make setup
make build
make upload
```

## Git branches

```sh
git switch arduboy # Arduboy2 sprite port and playground
git switch main    # joystick-controlled 3D renderer
git switch ble     # verified BLE text display bridge
git switch games   # Snake + Flappy Bird console
```
