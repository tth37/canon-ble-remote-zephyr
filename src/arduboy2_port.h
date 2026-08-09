#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ARDUBOY2_BLACK = 0,
    ARDUBOY2_WHITE = 1,
    ARDUBOY2_INVERT = 2,
} arduboy2_color_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t width;
    uint8_t height;
} arduboy2_rect_t;

void arduboy2_set_frame_rate(uint8_t frames_per_second);
bool arduboy2_next_frame(void);
bool arduboy2_every_x_frames(uint8_t frames);
uint32_t arduboy2_frame_count(void);

void arduboy2_draw_bitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                          uint8_t width, uint8_t height, arduboy2_color_t color);
void arduboy2_draw_overwrite(int16_t x, int16_t y, const uint8_t *sprite,
                             uint8_t frame);
void arduboy2_draw_self_masked(int16_t x, int16_t y, const uint8_t *sprite,
                               uint8_t frame);
void arduboy2_draw_erase(int16_t x, int16_t y, const uint8_t *sprite,
                         uint8_t frame);
void arduboy2_draw_plus_mask(int16_t x, int16_t y, const uint8_t *sprite,
                             uint8_t frame);
void arduboy2_draw_external_mask(int16_t x, int16_t y, const uint8_t *sprite,
                                 const uint8_t *mask, uint8_t frame,
                                 uint8_t mask_frame);

bool arduboy2_collide(arduboy2_rect_t first, arduboy2_rect_t second);
