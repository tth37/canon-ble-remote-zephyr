#pragma once

#include "driver/gpio.h"

// ESP32-C6-DevKitC-1 header labels used by the wiring guide.
#define DISPLAY_SDA_GPIO GPIO_NUM_6
#define DISPLAY_SCL_GPIO GPIO_NUM_7

#define DISPLAY_I2C_FREQUENCY_HZ 400000
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
