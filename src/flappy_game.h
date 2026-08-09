#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

void flappy_game_start(void);
bool flappy_game_button_pressed(void);
bool flappy_game_update(TickType_t now);
void flappy_game_render(void);
