#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "hal/adc_types.h"

// ESP32-C6-DevKitC-1 header labels used by the wiring guide.
#define DISPLAY_SDA_GPIO GPIO_NUM_6
#define DISPLAY_SCL_GPIO GPIO_NUM_7

#define DISPLAY_I2C_FREQUENCY_HZ 400000
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

// Five-pin analog joystick. Power the module from 3V3, not 5V.
#define JOYSTICK_X_GPIO GPIO_NUM_0
#define JOYSTICK_Y_GPIO GPIO_NUM_1
#define JOYSTICK_SW_GPIO GPIO_NUM_2
#define JOYSTICK_X_ADC_CHANNEL ADC_CHANNEL_0
#define JOYSTICK_Y_ADC_CHANNEL ADC_CHANNEL_1

#define JOYSTICK_DEAD_ZONE 700
#define JOYSTICK_CALIBRATION_SAMPLES 64
#define JOYSTICK_INVERT_X false
#define JOYSTICK_INVERT_Y false
