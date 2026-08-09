#include "snake_game.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/task.h"

#define CELL_SIZE 4
#define ORIGIN_X 4
#define ORIGIN_Y 12
#define GRID_WIDTH 30
#define GRID_HEIGHT 12
#define MAX_LENGTH (GRID_WIDTH * GRID_HEIGHT)
#define INITIAL_LENGTH 4
#define INITIAL_MOVE_INTERVAL_MS 180
#define MINIMUM_MOVE_INTERVAL_MS 70

typedef struct {
    uint8_t x;
    uint8_t y;
} point_t;

typedef enum {
    SNAKE_UP,
    SNAKE_DOWN,
    SNAKE_LEFT,
    SNAKE_RIGHT,
} snake_direction_t;

typedef enum {
    SNAKE_PLAYING,
    SNAKE_PAUSED,
    SNAKE_OVER,
    SNAKE_WON,
} snake_status_t;

static const char *TAG = "snake";
static point_t snake[MAX_LENGTH];
static point_t food;
static uint16_t snake_length;
static uint16_t score;
static snake_direction_t direction;
static snake_direction_t pending_direction;
static snake_status_t status;
static TickType_t next_move_at;

static bool points_equal(point_t first, point_t second)
{
    return first.x == second.x && first.y == second.y;
}

static bool snake_contains(point_t point, uint16_t count)
{
    for (uint16_t i = 0; i < count; ++i) {
        if (points_equal(snake[i], point)) {
            return true;
        }
    }
    return false;
}

static bool place_food(void)
{
    if (snake_length >= MAX_LENGTH) {
        return false;
    }
    for (int attempt = 0; attempt < MAX_LENGTH * 2; ++attempt) {
        const point_t candidate = {
            .x = (uint8_t)(esp_random() % GRID_WIDTH),
            .y = (uint8_t)(esp_random() % GRID_HEIGHT),
        };
        if (!snake_contains(candidate, snake_length)) {
            food = candidate;
            return true;
        }
    }
    for (uint8_t y = 0; y < GRID_HEIGHT; ++y) {
        for (uint8_t x = 0; x < GRID_WIDTH; ++x) {
            const point_t candidate = {.x = x, .y = y};
            if (!snake_contains(candidate, snake_length)) {
                food = candidate;
                return true;
            }
        }
    }
    return false;
}

static uint32_t move_interval_ms(void)
{
    const uint32_t speedup = (uint32_t)score * 4U;
    if (speedup >= INITIAL_MOVE_INTERVAL_MS - MINIMUM_MOVE_INTERVAL_MS) {
        return MINIMUM_MOVE_INTERVAL_MS;
    }
    return INITIAL_MOVE_INTERVAL_MS - speedup;
}

void snake_game_start(void)
{
    memset(snake, 0, sizeof(snake));
    snake_length = INITIAL_LENGTH;
    score = 0;
    direction = SNAKE_RIGHT;
    pending_direction = SNAKE_RIGHT;
    status = SNAKE_PLAYING;

    const uint8_t head_x = GRID_WIDTH / 2;
    const uint8_t head_y = GRID_HEIGHT / 2;
    for (uint16_t i = 0; i < snake_length; ++i) {
        snake[i] = (point_t){.x = (uint8_t)(head_x - i), .y = head_y};
    }
    (void)place_food();
    next_move_at = xTaskGetTickCount() + pdMS_TO_TICKS(INITIAL_MOVE_INTERVAL_MS);
    ESP_LOGI(TAG, "New game started");
}

static bool directions_are_opposite(snake_direction_t first, snake_direction_t second)
{
    return (first == SNAKE_UP && second == SNAKE_DOWN) ||
           (first == SNAKE_DOWN && second == SNAKE_UP) ||
           (first == SNAKE_LEFT && second == SNAKE_RIGHT) ||
           (first == SNAKE_RIGHT && second == SNAKE_LEFT);
}

void snake_game_handle_direction(input_direction_t input_direction)
{
    if (status != SNAKE_PLAYING || input_direction == INPUT_DIRECTION_NONE) {
        return;
    }

    snake_direction_t requested;
    switch (input_direction) {
        case INPUT_DIRECTION_UP: requested = SNAKE_UP; break;
        case INPUT_DIRECTION_DOWN: requested = SNAKE_DOWN; break;
        case INPUT_DIRECTION_LEFT: requested = SNAKE_LEFT; break;
        case INPUT_DIRECTION_RIGHT: requested = SNAKE_RIGHT; break;
        default: return;
    }
    if (!directions_are_opposite(direction, requested)) {
        pending_direction = requested;
    }
}

bool snake_game_button_pressed(void)
{
    if (status == SNAKE_OVER || status == SNAKE_WON) {
        snake_game_start();
    } else if (status == SNAKE_PLAYING) {
        status = SNAKE_PAUSED;
        ESP_LOGI(TAG, "Paused");
    } else {
        status = SNAKE_PLAYING;
        next_move_at = xTaskGetTickCount() + pdMS_TO_TICKS(move_interval_ms());
        ESP_LOGI(TAG, "Resumed");
    }
    return true;
}

static void step_game(void)
{
    direction = pending_direction;
    point_t next_head = snake[0];
    switch (direction) {
        case SNAKE_UP:
            next_head.y = next_head.y == 0 ? GRID_HEIGHT - 1 : next_head.y - 1;
            break;
        case SNAKE_DOWN:
            next_head.y = (uint8_t)((next_head.y + 1U) % GRID_HEIGHT);
            break;
        case SNAKE_LEFT:
            next_head.x = next_head.x == 0 ? GRID_WIDTH - 1 : next_head.x - 1;
            break;
        case SNAKE_RIGHT:
            next_head.x = (uint8_t)((next_head.x + 1U) % GRID_WIDTH);
            break;
    }

    const bool ate_food = points_equal(next_head, food);
    const uint16_t collision_count = ate_food ? snake_length : snake_length - 1U;
    if (snake_contains(next_head, collision_count)) {
        status = SNAKE_OVER;
        ESP_LOGI(TAG, "Game over at score %u", (unsigned)score);
        return;
    }

    if (ate_food && snake_length < MAX_LENGTH) {
        for (uint16_t i = snake_length; i > 0; --i) {
            snake[i] = snake[i - 1U];
        }
        ++snake_length;
    } else {
        for (uint16_t i = snake_length - 1U; i > 0; --i) {
            snake[i] = snake[i - 1U];
        }
    }
    snake[0] = next_head;

    if (ate_food) {
        ++score;
        if (!place_food()) {
            status = SNAKE_WON;
        }
    }
}

bool snake_game_update(TickType_t now)
{
    if (status != SNAKE_PLAYING || (int32_t)(now - next_move_at) < 0) {
        return false;
    }
    step_game();
    next_move_at = now + pdMS_TO_TICKS(move_interval_ms());
    return true;
}

static void draw_cell(point_t point, int inset)
{
    display_fill_rectangle(ORIGIN_X + point.x * CELL_SIZE + inset,
                           ORIGIN_Y + point.y * CELL_SIZE + inset,
                           CELL_SIZE - inset * 2, CELL_SIZE - inset * 2);
}

void snake_game_render(void)
{
    display_clear();
    if (status == SNAKE_PLAYING) {
        char score_text[20];
        (void)snprintf(score_text, sizeof(score_text), "SCORE %u", (unsigned)score);
        display_draw_text(1, 0, score_text, 1);
        display_draw_rectangle(2, 10, 124, 52);
        draw_cell(food, 1);
        for (uint16_t i = 0; i < snake_length; ++i) {
            draw_cell(snake[i], i == 0 ? 0 : 1);
        }
        return;
    }

    display_draw_rectangle(0, 0, 128, 64);
    if (status == SNAKE_PAUSED) {
        display_draw_centered_text(10, "PAUSED", 2);
        display_draw_centered_text(43, "PRESS SW", 1);
    } else {
        display_draw_centered_text(10, status == SNAKE_WON ? "YOU WIN" : "GAME OVER", 1);
        char score_text[20];
        (void)snprintf(score_text, sizeof(score_text), "SCORE %u", (unsigned)score);
        display_draw_centered_text(29, score_text, 1);
        display_draw_centered_text(46, "PRESS SW", 1);
    }
}
