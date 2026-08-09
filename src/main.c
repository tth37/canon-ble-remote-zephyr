#include <stdbool.h>

#include "display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "flappy_game.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input.h"
#include "snake_game.h"

#define INPUT_POLL_MS 20

typedef enum {
    APP_MENU,
    APP_SNAKE,
    APP_FLAPPY,
} app_mode_t;

typedef enum {
    MENU_SNAKE,
    MENU_FLAPPY,
} menu_selection_t;

static const char *TAG = "game_console";

static void render_calibration_screen(void)
{
    display_clear();
    display_draw_rectangle(0, 0, 128, 64);
    display_draw_centered_text(15, "RELEASE STICK", 1);
    display_draw_centered_text(33, "CALIBRATING", 1);
}

static void render_menu(menu_selection_t selection)
{
    display_clear();
    display_draw_rectangle(0, 0, 128, 64);
    display_draw_centered_text(4, "GAME MENU", 1);

    const int snake_y = 21;
    const int flappy_y = 35;
    display_draw_text(27, snake_y, "SNAKE", 1);
    display_draw_text(27, flappy_y, "FLAPPY", 1);
    display_fill_rectangle(15, (selection == MENU_SNAKE ? snake_y : flappy_y) + 2, 5, 4);
    display_draw_centered_text(53, "SW SELECT", 1);
}

static void render_mode(app_mode_t mode, menu_selection_t selection)
{
    switch (mode) {
        case APP_MENU: render_menu(selection); break;
        case APP_SNAKE: snake_game_render(); break;
        case APP_FLAPPY: flappy_game_render(); break;
    }
    ESP_ERROR_CHECK(display_present());
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_initialize());
    ESP_ERROR_CHECK(input_initialize());

    render_calibration_screen();
    ESP_ERROR_CHECK(display_present());
    input_calibrate_joystick();

    app_mode_t mode = APP_MENU;
    menu_selection_t selection = MENU_SNAKE;
    render_mode(mode, selection);
    ESP_LOGI(TAG, "Game menu ready");

    while (true) {
        const TickType_t now = xTaskGetTickCount();
        const input_direction_t input_direction = input_read_direction();
        const input_button_event_t button_event = input_read_button(now);
        bool redraw = false;

        if (mode != APP_MENU && button_event == INPUT_BUTTON_LONG_PRESS) {
            mode = APP_MENU;
            redraw = true;
            ESP_LOGI(TAG, "Returned to menu");
        } else if (mode == APP_MENU) {
            if (input_direction == INPUT_DIRECTION_UP ||
                input_direction == INPUT_DIRECTION_LEFT) {
                selection = MENU_SNAKE;
                redraw = true;
            } else if (input_direction == INPUT_DIRECTION_DOWN ||
                       input_direction == INPUT_DIRECTION_RIGHT) {
                selection = MENU_FLAPPY;
                redraw = true;
            }

            if (button_event == INPUT_BUTTON_PRESSED) {
                if (selection == MENU_SNAKE) {
                    snake_game_start();
                    mode = APP_SNAKE;
                } else {
                    flappy_game_start();
                    mode = APP_FLAPPY;
                }
                redraw = true;
            }
        } else if (mode == APP_SNAKE) {
            snake_game_handle_direction(input_direction);
            if (button_event == INPUT_BUTTON_PRESSED) {
                redraw = snake_game_button_pressed();
            }
            redraw = snake_game_update(now) || redraw;
        } else {
            if (button_event == INPUT_BUTTON_PRESSED) {
                redraw = flappy_game_button_pressed();
            }
            redraw = flappy_game_update(now) || redraw;
        }

        if (redraw) {
            render_mode(mode, selection);
        }
        vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_MS));
    }
}
