#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t display_initialize(void);
void display_clear(void);
void display_draw_pixel(int x, int y, bool on);
void display_fill_rectangle(int x, int y, int width, int height);
void display_draw_rectangle(int x, int y, int width, int height);
void display_draw_text(int x, int y, const char *text, int scale);
void display_draw_centered_text(int y, const char *text, int scale);
void display_draw_centered_text_in_region(int region_x, int region_width, int y,
                                          const char *text, int scale);
uint8_t *display_get_framebuffer(void);
esp_err_t display_present(void);
