#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "arduboy2_port.h"
#include "demo_sprites.h"
#include "display.h"
#include "display_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "joystick.h"

#define DEMO_FRAME_RATE 30
#define PLAYFIELD_TOP 12
#define ROBOT_SIZE 16
#define ENERGY_SIZE 8
#define STAR_COUNT 18

static const char *TAG = "arduboy_demo";
static uint32_t random_state = 0xC6A2D2U;

static uint32_t next_random(void)
{
    random_state = random_state * 1664525U + 1013904223U;
    return random_state;
}

static float clamp_position(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void place_energy(int16_t *x, int16_t *y)
{
    *x = (int16_t)(4 + next_random() % (DISPLAY_WIDTH - ENERGY_SIZE - 8));
    *y = (int16_t)(PLAYFIELD_TOP + 2 +
                   next_random() % (DISPLAY_HEIGHT - PLAYFIELD_TOP - ENERGY_SIZE - 4));
}

static void draw_starfield(uint32_t frame)
{
    for (int star = 0; star < STAR_COUNT; ++star) {
        const int speed = star % 3 + 1;
        const int x = (star * 37 + (int)(frame / 3U) * speed) % DISPLAY_WIDTH;
        const int y = PLAYFIELD_TOP + (star * 23) % (DISPLAY_HEIGHT - PLAYFIELD_TOP);
        display_draw_pixel(x, y, true);
    }
}

static void draw_warp_burst(int center_x, int center_y, uint8_t burst_frame)
{
    static const int8_t directions[][2] = {
        {-2, -1}, {-1, -2}, {1, -2}, {2, -1},
        {2, 1}, {1, 2}, {-1, 2}, {-2, 1},
    };
    const int distance = 1 + (12 - burst_frame) / 2;
    for (size_t index = 0; index < sizeof(directions) / sizeof(directions[0]); ++index) {
        display_draw_pixel(center_x + directions[index][0] * distance,
                           center_y + directions[index][1] * distance, true);
    }
}

static void draw_demo(float robot_x, float robot_y, int16_t energy_x, int16_t energy_y,
                      uint8_t robot_frame, uint8_t burst_frame, unsigned score)
{
    char score_text[12];
    snprintf(score_text, sizeof(score_text), "SCORE %u", score);

    display_clear();
    display_draw_text(1, 1, score_text, 1);
    display_draw_text(80, 1, "SW WARP", 1);
    display_fill_rectangle(0, 9, DISPLAY_WIDTH, 1);
    draw_starfield(arduboy2_frame_count());
    arduboy2_draw_plus_mask(energy_x, energy_y, energy_sprite, 0);
    arduboy2_draw_plus_mask((int16_t)robot_x, (int16_t)robot_y, robot_sprite,
                            robot_frame);
    if (burst_frame > 0) {
        draw_warp_burst((int)robot_x + ROBOT_SIZE / 2,
                        (int)robot_y + ROBOT_SIZE / 2, burst_frame);
    }
    ESP_ERROR_CHECK(display_present());
}

static void show_calibration(void)
{
    display_clear();
    display_draw_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    display_draw_centered_text(15, "ARDUBOY C6", 1);
    display_draw_centered_text(29, "RELEASE STICK", 1);
    display_draw_centered_text(43, "CALIBRATING", 1);
    ESP_ERROR_CHECK(display_present());
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_initialize());
    ESP_ERROR_CHECK(joystick_initialize());
    show_calibration();
    joystick_calibrate();

    arduboy2_set_frame_rate(DEMO_FRAME_RATE);
    float robot_x = 24.0F;
    float robot_y = 28.0F;
    int16_t energy_x = 96;
    int16_t energy_y = 38;
    uint8_t robot_frame = 0;
    uint8_t burst_frame = 0;
    unsigned score = 0;

    ESP_LOGI(TAG, "Arduboy2 sprite demo started at %d FPS", DEMO_FRAME_RATE);
    while (true) {
        if (!arduboy2_next_frame()) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        joystick_state_t joystick;
        joystick_read(&joystick);
        const bool moving = joystick.x != 0.0F || joystick.y != 0.0F;
        robot_x = clamp_position(robot_x + joystick.x * 2.5F, 0.0F,
                                 DISPLAY_WIDTH - ROBOT_SIZE);
        robot_y = clamp_position(robot_y + joystick.y * 2.5F, PLAYFIELD_TOP,
                                 DISPLAY_HEIGHT - ROBOT_SIZE);

        if (moving && arduboy2_every_x_frames(5)) {
            robot_frame ^= 1U;
        }
        if (joystick.button_pressed) {
            robot_x = (float)(next_random() % (DISPLAY_WIDTH - ROBOT_SIZE));
            robot_y = (float)(PLAYFIELD_TOP +
                              next_random() % (DISPLAY_HEIGHT - PLAYFIELD_TOP - ROBOT_SIZE));
            burst_frame = 12;
        } else if (burst_frame > 0) {
            --burst_frame;
        }

        const arduboy2_rect_t robot_box = {
            .x = (int16_t)robot_x + 2,
            .y = (int16_t)robot_y + 2,
            .width = ROBOT_SIZE - 4,
            .height = ROBOT_SIZE - 4,
        };
        const arduboy2_rect_t energy_box = {
            .x = energy_x,
            .y = energy_y,
            .width = ENERGY_SIZE,
            .height = ENERGY_SIZE,
        };
        if (arduboy2_collide(robot_box, energy_box)) {
            ++score;
            place_energy(&energy_x, &energy_y);
            burst_frame = 12;
            ESP_LOGI(TAG, "Energy collected; score=%u", score);
        }
        draw_demo(robot_x, robot_y, energy_x, energy_y, robot_frame, burst_frame,
                  score);
    }
}
