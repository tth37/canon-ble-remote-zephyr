#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef enum {
    INPUT_DIRECTION_NONE,
    INPUT_DIRECTION_UP,
    INPUT_DIRECTION_DOWN,
    INPUT_DIRECTION_LEFT,
    INPUT_DIRECTION_RIGHT,
} input_direction_t;

typedef enum {
    INPUT_BUTTON_NONE,
    INPUT_BUTTON_PRESSED,
    INPUT_BUTTON_LONG_PRESS,
} input_button_event_t;

esp_err_t input_initialize(void);
void input_calibrate_joystick(void);
input_direction_t input_read_direction(void);
input_button_event_t input_read_button(TickType_t now);
