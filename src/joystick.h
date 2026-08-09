#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    float x;
    float y;
    bool button_pressed;
} joystick_state_t;

esp_err_t joystick_initialize(void);
void joystick_calibrate(void);
void joystick_read(joystick_state_t *state);
