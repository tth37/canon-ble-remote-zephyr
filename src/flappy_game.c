#include "flappy_game.h"

#include <stdint.h>
#include <stdio.h>

#include "display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/task.h"

#define FRAME_INTERVAL_MS 40
#define PLAY_TOP 9
#define PLAY_BOTTOM 63
#define BIRD_X 20
#define BIRD_WIDTH 5
#define BIRD_HEIGHT 4
#define PIPE_COUNT 3
#define PIPE_WIDTH 10
#define PIPE_GAP 24
#define PIPE_SPACING 50
#define PIPE_SPEED 2
#define GRAVITY_TENTHS 4
#define FLAP_VELOCITY_TENTHS -30
#define TERMINAL_VELOCITY_TENTHS 28

typedef struct {
    int16_t x;
    uint8_t gap_top;
    bool scored;
} pipe_t;

static const char *TAG = "flappy";
static pipe_t pipes[PIPE_COUNT];
static int32_t bird_y_tenths;
static int32_t bird_velocity_tenths;
static uint16_t score;
static bool game_over;
static TickType_t next_frame_at;

static uint8_t random_gap_top(void)
{
    const uint8_t minimum = PLAY_TOP + 3;
    const uint8_t maximum = PLAY_BOTTOM - PIPE_GAP - 1;
    return (uint8_t)(minimum + esp_random() % (maximum - minimum + 1U));
}

void flappy_game_start(void)
{
    bird_y_tenths = 30 * 10;
    bird_velocity_tenths = 0;
    score = 0;
    game_over = false;
    for (int i = 0; i < PIPE_COUNT; ++i) {
        pipes[i] = (pipe_t){
            .x = (int16_t)(100 + i * PIPE_SPACING),
            .gap_top = random_gap_top(),
            .scored = false,
        };
    }
    next_frame_at = xTaskGetTickCount() + pdMS_TO_TICKS(FRAME_INTERVAL_MS);
    ESP_LOGI(TAG, "New game started");
}

bool flappy_game_button_pressed(void)
{
    if (game_over) {
        flappy_game_start();
    } else {
        bird_velocity_tenths = FLAP_VELOCITY_TENTHS;
    }
    return true;
}

static bool bird_hits_pipe(const pipe_t *pipe, int bird_y)
{
    const bool overlaps_x = BIRD_X + BIRD_WIDTH > pipe->x && BIRD_X < pipe->x + PIPE_WIDTH;
    if (!overlaps_x) {
        return false;
    }
    return bird_y < pipe->gap_top || bird_y + BIRD_HEIGHT > pipe->gap_top + PIPE_GAP;
}

static void reset_offscreen_pipe(pipe_t *pipe)
{
    int16_t furthest_x = pipes[0].x;
    for (int i = 1; i < PIPE_COUNT; ++i) {
        if (pipes[i].x > furthest_x) {
            furthest_x = pipes[i].x;
        }
    }
    pipe->x = (int16_t)(furthest_x + PIPE_SPACING);
    pipe->gap_top = random_gap_top();
    pipe->scored = false;
}

static void step_game(void)
{
    bird_velocity_tenths += GRAVITY_TENTHS;
    if (bird_velocity_tenths > TERMINAL_VELOCITY_TENTHS) {
        bird_velocity_tenths = TERMINAL_VELOCITY_TENTHS;
    }
    bird_y_tenths += bird_velocity_tenths;
    const int bird_y = (int)(bird_y_tenths / 10);

    for (int i = 0; i < PIPE_COUNT; ++i) {
        pipes[i].x -= PIPE_SPEED;
        if (!pipes[i].scored && pipes[i].x + PIPE_WIDTH < BIRD_X) {
            pipes[i].scored = true;
            ++score;
        }
        if (pipes[i].x + PIPE_WIDTH < 0) {
            reset_offscreen_pipe(&pipes[i]);
        }
    }

    if (bird_y < PLAY_TOP || bird_y + BIRD_HEIGHT > PLAY_BOTTOM) {
        game_over = true;
    }
    for (int i = 0; i < PIPE_COUNT && !game_over; ++i) {
        game_over = bird_hits_pipe(&pipes[i], bird_y);
    }
    if (game_over) {
        ESP_LOGI(TAG, "Game over at score %u", (unsigned)score);
    }
}

bool flappy_game_update(TickType_t now)
{
    if (game_over || (int32_t)(now - next_frame_at) < 0) {
        return false;
    }
    step_game();
    next_frame_at = now + pdMS_TO_TICKS(FRAME_INTERVAL_MS);
    return true;
}

void flappy_game_render(void)
{
    display_clear();
    if (game_over) {
        display_draw_rectangle(0, 0, 128, 64);
        display_draw_centered_text(8, "GAME OVER", 1);
        char score_text[20];
        (void)snprintf(score_text, sizeof(score_text), "SCORE %u", (unsigned)score);
        display_draw_centered_text(27, score_text, 1);
        display_draw_centered_text(45, "PRESS SW", 1);
        return;
    }

    char score_text[20];
    (void)snprintf(score_text, sizeof(score_text), "SCORE %u", (unsigned)score);
    display_draw_text(1, 0, score_text, 1);
    display_fill_rectangle(0, PLAY_TOP - 1, 128, 1);
    display_fill_rectangle(0, PLAY_BOTTOM, 128, 1);

    for (int i = 0; i < PIPE_COUNT; ++i) {
        const int lower_pipe_y = pipes[i].gap_top + PIPE_GAP;
        display_fill_rectangle(pipes[i].x, PLAY_TOP, PIPE_WIDTH,
                               pipes[i].gap_top - PLAY_TOP);
        display_fill_rectangle(pipes[i].x, lower_pipe_y, PIPE_WIDTH,
                               PLAY_BOTTOM - lower_pipe_y);
    }

    const int bird_y = (int)(bird_y_tenths / 10);
    display_fill_rectangle(BIRD_X, bird_y, BIRD_WIDTH, BIRD_HEIGHT);
    display_draw_pixel(BIRD_X + BIRD_WIDTH, bird_y + 2, true);
    display_draw_pixel(BIRD_X + 3, bird_y, false);
}
