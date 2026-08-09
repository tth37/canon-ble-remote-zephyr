#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    float x;
    float y;
    bool button_down;
    bool button_pressed;
    bool button_released;
} joystick_state_t;

typedef struct {
    joystick_state_t left;
    joystick_state_t right;
} dual_joystick_state_t;

esp_err_t joysticks_initialize(void);
void joysticks_calibrate(void);
void joysticks_read(dual_joystick_state_t *state);
