# Rift Runner for ESP32-C6

`dual-rift` is a single-player twin-stick action game for an
ESP32-C6-DevKitC-1, two 128x64 SSD1306 OLEDs, and two analog joysticks. The
OLEDs form one continuous 256x64 playfield: move on the left stick and aim on
the right.

The two OLEDs may both have the fixed I2C address `0x3C`. They are placed on
separate hardware controllers: the ESP32-C6 low-power I2C controller drives
the left display on GPIO6/7, while the regular I2C controller drives the right
display on GPIO22/23. No address modification or I2C multiplexer is needed.

## Wiring

Disconnect USB power while changing wires. Place the left and right displays
physically beside each other in that order.

| Device | Module pin | ESP32-C6 pin |
| --- | --- | --- |
| Left display | VCC | 3V3 |
| Left display | GND | GND |
| Left display | SDA | GPIO6 |
| Left display | SCL | GPIO7 |
| Right display | VCC | 3V3 |
| Right display | GND | GND |
| Right display | SDA | GPIO22 |
| Right display | SCL | GPIO23 |
| Left joystick | `+5V` / `+` | **3V3, not 5V** |
| Left joystick | GND | GND |
| Left joystick | VRx | GPIO0 / ADC1_CH0 |
| Left joystick | VRy | GPIO1 / ADC1_CH1 |
| Left joystick | SW | GPIO20 |
| Right joystick | `+5V` / `+` | **3V3, not 5V** |
| Right joystick | GND | GND |
| Right joystick | VRx | GPIO2 / ADC1_CH2 |
| Right joystick | VRy | GPIO3 / ADC1_CH3 |
| Right joystick | SW | GPIO21 |

All four modules must share ground with the ESP32-C6. Leave both joysticks
centered and released during the short startup calibration.

## Controls

- Left stick: move with analog speed
- Left SW: dash; the HUD bar shows its cooldown
- Right stick: aim in eight sprite directions
- Hold right SW: rapid fire
- Hold both SW buttons for about 1.5 seconds: pause
- Either SW: start, resume, or retry

Enemies enter from both outer edges and chase the player across the two-screen
arena. Every seventh kill drops a core: collecting it restores one health, or
awards 25 points if health is already full. Enemy health, speed, and spawn rate
increase with each wave. The high score lasts until the board restarts.

If an axis moves in the wrong direction, change its `JOYSTICK_*_INVERT_X` or
`JOYSTICK_*_INVERT_Y` setting in `src/display_config.h` and rebuild.

## Source layout

- `src/display.c`: one 256x64 framebuffer split across two hardware I2C buses
- `src/joystick.c`: four ADC axes, two switches, and startup calibration
- `src/arduboy2_port.c`: sprite drawing, collision, and fixed-frame timing
- `src/rift_runner.c`: game state, combat, progression, and rendering
- `src/rift_assets.h`: ship, enemy, and core sprites

The Arduboy2-derived API and license information are in
`third_party/arduboy2/`. Arduino, AVR assembly, audio, EEPROM, USB, and the
original Arduboy hardware layer are intentionally not included.

## Build and upload

The Python/PlatformIO toolchain stays inside `.venv` and `.platformio`:

```sh
make setup
make build
```

Building does not write to the board. Upload only after the wiring is checked:

```sh
make upload
```

## Git branches

```sh
git switch dual-rift # dual-screen twin-stick Rift Runner
git switch arduboy   # Arduboy2 sprite port and playground
git switch main      # joystick-controlled 3D renderer
git switch ble       # BLE text display bridge
git switch games     # Snake + Flappy Bird console
```
