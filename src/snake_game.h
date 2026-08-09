#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "input.h"

void snake_game_start(void);
void snake_game_handle_direction(input_direction_t direction);
bool snake_game_button_pressed(void);
bool snake_game_update(TickType_t now);
void snake_game_render(void);
