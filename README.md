# ESP32-C6 OLED Snake game

A tiny game console for an ESP32-C6-DevKitC-1, a four-pin I2C OLED, and a
five-pin analog joystick. It currently includes Snake and Flappy Bird.

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

Do not use 5V unless the display module explicitly guarantees that its I2C
pull-ups and logic are safe for a 3.3V microcontroller.

## Supported first target

- SSD1306 controller
- 128x64 monochrome OLED
- I2C address 0x3C or 0x3D (detected automatically)

The serial log reports every detected I2C address. This makes wiring and
controller mismatches visible even if the screen cannot yet be initialized.

## Menu and controls

Leave the joystick centered while the startup calibration screen is visible.
In the menu, move up/down (or left/right) to select a game and press SW to
launch it. Hold SW for about one second from either game to return to the menu.

### Snake

Move the joystick up, down, left, or right to steer, returning it near center
between turns. A short SW press pauses/resumes or restarts after a collision.
Crossing an edge wraps the snake around to the opposite side of the board.

The game begins at a 180 ms movement interval and speeds up as food is eaten.

### Flappy Bird

Press SW briefly to flap. Pass through pipe gaps to score; after a collision,
press SW to restart.

If an axis runs backwards for the physical orientation of your module, change
`JOYSTICK_INVERT_X` or `JOYSTICK_INVERT_Y` in `src/display_config.h` to `true`.

## Build, upload, and monitor

Set up the project-local Python environment, then build, upload, and monitor:

```sh
make setup
make build
make upload
make monitor
```

`make setup` creates `.venv` for PlatformIO. The Makefile also points
`PLATFORMIO_CORE_DIR` at `.platformio`, so the compiler, ESP-IDF framework,
and upload tools remain inside this project instead of being installed in a
user-global tool directory.

The configured serial device is `/dev/cu.usbserial-310`.
