/*
 * ESP-IDF adaptation of Arduboy2 frame, bitmap, sprite, and collision code.
 * Upstream: https://github.com/MLXXXp/Arduboy2 at bc460a2cff1a3e116880991aa2f88bae4b2e3160
 * See third_party/arduboy2/LICENSE.txt.
 */

#include "arduboy2_port.h"

#include <stddef.h>

#include "display.h"
#include "display_config.h"
#include "esp_timer.h"

typedef enum {
    DRAW_OVERWRITE,
    DRAW_SELF_MASKED,
    DRAW_ERASE,
    DRAW_PLUS_MASK,
    DRAW_EXTERNAL_MASK,
} sprite_draw_mode_t;

static int64_t frame_duration_us = 1000000 / 30;
static int64_t next_frame_us;
static uint32_t frame_counter;

void arduboy2_set_frame_rate(uint8_t frames_per_second)
{
    if (frames_per_second == 0) {
        frames_per_second = 1;
    }
    frame_duration_us = 1000000 / frames_per_second;
    next_frame_us = esp_timer_get_time();
    frame_counter = 0;
}

bool arduboy2_next_frame(void)
{
    const int64_t now = esp_timer_get_time();
    if (now < next_frame_us) {
        return false;
    }

    next_frame_us += frame_duration_us;
    if (now - next_frame_us > frame_duration_us * 4) {
        next_frame_us = now + frame_duration_us;
    }
    ++frame_counter;
    return true;
}

bool arduboy2_every_x_frames(uint8_t frames)
{
    return frames != 0 && frame_counter % frames == 0;
}

uint32_t arduboy2_frame_count(void)
{
    return frame_counter;
}

static void composite_byte(int x, int page, uint16_t pixels, uint16_t mask)
{
    if (x < 0 || x >= DISPLAY_WIDTH) {
        return;
    }

    uint8_t *buffer = display_get_framebuffer();
    if (page >= 0 && page < DISPLAY_HEIGHT / 8) {
        const size_t index = (size_t)page * DISPLAY_WIDTH + (size_t)x;
        buffer[index] = (uint8_t)((buffer[index] & (uint8_t)~mask) | pixels);
    }
    if (page + 1 >= 0 && page + 1 < DISPLAY_HEIGHT / 8) {
        const size_t index = (size_t)(page + 1) * DISPLAY_WIDTH + (size_t)x;
        buffer[index] =
            (uint8_t)((buffer[index] & (uint8_t)~(mask >> 8)) | (pixels >> 8));
    }
}

static void draw_sprite_data(int16_t x, int16_t y, const uint8_t *bitmap,
                             const uint8_t *mask, uint8_t width, uint8_t height,
                             sprite_draw_mode_t mode)
{
    if (bitmap == NULL || x + width <= 0 || x >= DISPLAY_WIDTH ||
        y + height <= 0 || y >= DISPLAY_HEIGHT) {
        return;
    }

    const uint8_t y_offset = (uint8_t)y & 7U;
    int destination_page = y / 8;
    if (y < 0 && y_offset != 0) {
        --destination_page;
    }
    const uint8_t source_pages = (uint8_t)((height + 7U) / 8U);
    const uint8_t stride = mode == DRAW_PLUS_MASK ? 2U : 1U;

    for (uint8_t source_page = 0; source_page < source_pages; ++source_page) {
        for (uint8_t column = 0; column < width; ++column) {
            const size_t source_index =
                ((size_t)source_page * width + column) * stride;
            const uint8_t source_pixels = bitmap[source_index];
            uint8_t source_mask = 0xFF;

            if (mode == DRAW_SELF_MASKED || mode == DRAW_ERASE) {
                source_mask = source_pixels;
            } else if (mode == DRAW_PLUS_MASK) {
                source_mask = bitmap[source_index + 1U];
            } else if (mode == DRAW_EXTERNAL_MASK) {
                source_mask = mask[(size_t)source_page * width + column];
            }

            uint16_t pixels = (uint16_t)source_pixels << y_offset;
            const uint16_t shifted_mask = (uint16_t)source_mask << y_offset;
            if (mode == DRAW_ERASE) {
                pixels = 0;
            }
            composite_byte(x + column, destination_page + source_page, pixels,
                           shifted_mask);
        }
    }
}

void arduboy2_draw_bitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                          uint8_t width, uint8_t height, arduboy2_color_t color)
{
    if (color == ARDUBOY2_WHITE) {
        draw_sprite_data(x, y, bitmap, NULL, width, height, DRAW_SELF_MASKED);
        return;
    }
    if (color == ARDUBOY2_BLACK) {
        draw_sprite_data(x, y, bitmap, NULL, width, height, DRAW_ERASE);
        return;
    }

    if (bitmap == NULL) {
        return;
    }
    const uint8_t pages = (uint8_t)((height + 7U) / 8U);
    uint8_t *buffer = display_get_framebuffer();
    for (uint8_t page = 0; page < pages; ++page) {
        for (uint8_t column = 0; column < width; ++column) {
            const uint8_t bits = bitmap[(size_t)page * width + column];
            for (uint8_t bit = 0; bit < 8U; ++bit) {
                if (page * 8U + bit >= height) {
                    break;
                }
                const int pixel_y = y + page * 8 + bit;
                const int pixel_x = x + column;
                if ((bits & (1U << bit)) != 0 && pixel_x >= 0 &&
                    pixel_x < DISPLAY_WIDTH && pixel_y >= 0 &&
                    pixel_y < DISPLAY_HEIGHT) {
                    const size_t index = (size_t)(pixel_y / 8) * DISPLAY_WIDTH +
                                         (size_t)pixel_x;
                    buffer[index] ^= (uint8_t)(1U << (pixel_y & 7));
                }
            }
        }
    }
}

static void draw_sprite(int16_t x, int16_t y, const uint8_t *sprite,
                        const uint8_t *mask, uint8_t frame, uint8_t mask_frame,
                        sprite_draw_mode_t mode)
{
    if (sprite == NULL) {
        return;
    }
    const uint8_t width = sprite[0];
    const uint8_t height = sprite[1];
    const size_t bytes_per_frame = (size_t)width * ((height + 7U) / 8U);
    const size_t sprite_stride = mode == DRAW_PLUS_MASK ? 2U : 1U;
    const uint8_t *bitmap =
        sprite + 2U + (size_t)frame * bytes_per_frame * sprite_stride;
    const uint8_t *frame_mask = mask;
    if (mask != NULL) {
        frame_mask += (size_t)mask_frame * bytes_per_frame;
    }
    draw_sprite_data(x, y, bitmap, frame_mask, width, height, mode);
}

void arduboy2_draw_overwrite(int16_t x, int16_t y, const uint8_t *sprite,
                             uint8_t frame)
{
    draw_sprite(x, y, sprite, NULL, frame, 0, DRAW_OVERWRITE);
}

void arduboy2_draw_self_masked(int16_t x, int16_t y, const uint8_t *sprite,
                               uint8_t frame)
{
    draw_sprite(x, y, sprite, NULL, frame, 0, DRAW_SELF_MASKED);
}

void arduboy2_draw_erase(int16_t x, int16_t y, const uint8_t *sprite,
                         uint8_t frame)
{
    draw_sprite(x, y, sprite, NULL, frame, 0, DRAW_ERASE);
}

void arduboy2_draw_plus_mask(int16_t x, int16_t y, const uint8_t *sprite,
                             uint8_t frame)
{
    draw_sprite(x, y, sprite, NULL, frame, 0, DRAW_PLUS_MASK);
}

void arduboy2_draw_external_mask(int16_t x, int16_t y, const uint8_t *sprite,
                                 const uint8_t *mask, uint8_t frame,
                                 uint8_t mask_frame)
{
    if (mask == NULL) {
        return;
    }
    draw_sprite(x, y, sprite, mask, frame, mask_frame, DRAW_EXTERNAL_MASK);
}

bool arduboy2_collide(arduboy2_rect_t first, arduboy2_rect_t second)
{
    return !(second.x >= first.x + first.width ||
             second.x + second.width <= first.x ||
             second.y >= first.y + first.height ||
             second.y + second.height <= first.y);
}
