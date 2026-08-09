#include "rift_runner.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arduboy2_port.h"
#include "display.h"
#include "display_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "joystick.h"
#include "rift_assets.h"

#define GAME_FRAME_RATE 24
#define PLAYFIELD_TOP 11
#define PLAYER_SIZE 8
#define ENEMY_SIZE 8
#define CORE_SIZE 8
#define BULLET_SIZE 2
#define MAX_BULLETS 20
#define MAX_ENEMIES 14
#define PLAYER_SPEED 2.4F
#define BULLET_SPEED 5.0F
#define DASH_DISTANCE 19.0F
#define DASH_COOLDOWN_FRAMES 42
#define FIRE_COOLDOWN_FRAMES 4
#define HIT_INVULNERABILITY_FRAMES 30
#define STARTING_HEALTH 3
#define PAUSE_HOLD_FRAMES 36

typedef enum {
    SCREEN_TITLE,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_GAME_OVER,
} screen_state_t;

typedef struct {
    float x;
    float y;
    float velocity_x;
    float velocity_y;
    bool active;
} bullet_t;

typedef struct {
    float x;
    float y;
    uint8_t health;
    uint8_t flash_frames;
    bool active;
} enemy_t;

typedef struct {
    screen_state_t screen;
    float player_x;
    float player_y;
    float aim_x;
    float aim_y;
    uint8_t aim_frame;
    uint8_t health;
    uint8_t fire_cooldown;
    uint8_t dash_cooldown;
    uint8_t invulnerability;
    uint8_t both_button_frames;
    uint16_t spawn_timer;
    float core_x;
    float core_y;
    bool core_active;
    unsigned score;
    unsigned high_score;
    unsigned kills;
    unsigned wave;
    uint32_t random_state;
    bullet_t bullets[MAX_BULLETS];
    enemy_t enemies[MAX_ENEMIES];
} game_t;

static const char *TAG = "rift_runner";

static float clamp_value(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static uint32_t next_random(game_t *game)
{
    game->random_state = game->random_state * 1664525U + 1013904223U;
    return game->random_state;
}

static void limit_magnitude(float *x, float *y)
{
    const float magnitude = sqrtf(*x * *x + *y * *y);
    if (magnitude > 1.0F) {
        *x /= magnitude;
        *y /= magnitude;
    }
}

static void normalize_direction(float *x, float *y)
{
    const float magnitude = sqrtf(*x * *x + *y * *y);
    if (magnitude > 0.001F) {
        *x /= magnitude;
        *y /= magnitude;
    }
}

static uint8_t aim_frame(float x, float y)
{
    const float absolute_x = x < 0.0F ? -x : x;
    const float absolute_y = y < 0.0F ? -y : y;
    if (absolute_x > absolute_y * 2.0F) {
        return x >= 0.0F ? 0 : 4;
    }
    if (absolute_y > absolute_x * 2.0F) {
        return y >= 0.0F ? 2 : 6;
    }
    if (x >= 0.0F) {
        return y >= 0.0F ? 1 : 7;
    }
    return y >= 0.0F ? 3 : 5;
}

static void reset_game(game_t *game)
{
    const unsigned high_score = game->high_score;
    memset(game, 0, sizeof(*game));
    game->screen = SCREEN_PLAYING;
    game->player_x = DISPLAY_WIDTH / 2.0F - PLAYER_SIZE / 2.0F;
    game->player_y = (PLAYFIELD_TOP + DISPLAY_HEIGHT) / 2.0F -
                     PLAYER_SIZE / 2.0F;
    game->aim_x = 1.0F;
    game->aim_y = 0.0F;
    game->health = STARTING_HEALTH;
    game->wave = 1;
    game->spawn_timer = 20;
    game->random_state = 0xC6D00D42U;
    game->high_score = high_score;
}

static void fire_bullet(game_t *game)
{
    for (int index = 0; index < MAX_BULLETS; ++index) {
        bullet_t *bullet = &game->bullets[index];
        if (bullet->active) {
            continue;
        }
        bullet->active = true;
        bullet->x = game->player_x + PLAYER_SIZE / 2.0F - 1.0F;
        bullet->y = game->player_y + PLAYER_SIZE / 2.0F - 1.0F;
        bullet->velocity_x = game->aim_x * BULLET_SPEED;
        bullet->velocity_y = game->aim_y * BULLET_SPEED;
        game->fire_cooldown = FIRE_COOLDOWN_FRAMES;
        return;
    }
}

static int active_enemy_count(const game_t *game)
{
    int count = 0;
    for (int index = 0; index < MAX_ENEMIES; ++index) {
        if (game->enemies[index].active) {
            ++count;
        }
    }
    return count;
}

static void spawn_enemy(game_t *game)
{
    for (int index = 0; index < MAX_ENEMIES; ++index) {
        enemy_t *enemy = &game->enemies[index];
        if (enemy->active) {
            continue;
        }
        enemy->active = true;
        enemy->x = (next_random(game) & 1U) != 0 ? 1.0F
                                                  : DISPLAY_WIDTH - ENEMY_SIZE - 1.0F;
        enemy->y = (float)(PLAYFIELD_TOP +
                           next_random(game) % (DISPLAY_HEIGHT - PLAYFIELD_TOP -
                                                ENEMY_SIZE));
        enemy->health = (uint8_t)(1U + game->wave / 4U);
        if (enemy->health > 3U) {
            enemy->health = 3U;
        }
        enemy->flash_frames = 0;
        return;
    }
}

static void update_player(game_t *game, const dual_joystick_state_t *input)
{
    float move_x = input->left.x;
    float move_y = input->left.y;
    limit_magnitude(&move_x, &move_y);
    game->player_x += move_x * PLAYER_SPEED;
    game->player_y += move_y * PLAYER_SPEED;

    if (input->left.button_pressed && game->dash_cooldown == 0) {
        float dash_x = move_x;
        float dash_y = move_y;
        if (dash_x == 0.0F && dash_y == 0.0F) {
            dash_x = game->aim_x;
            dash_y = game->aim_y;
        }
        normalize_direction(&dash_x, &dash_y);
        game->player_x += dash_x * DASH_DISTANCE;
        game->player_y += dash_y * DASH_DISTANCE;
        game->dash_cooldown = DASH_COOLDOWN_FRAMES;
        game->invulnerability = 8;
    }

    game->player_x = clamp_value(game->player_x, 0.0F,
                                 DISPLAY_WIDTH - PLAYER_SIZE);
    game->player_y = clamp_value(game->player_y, PLAYFIELD_TOP,
                                 DISPLAY_HEIGHT - PLAYER_SIZE);

    float aim_x = input->right.x;
    float aim_y = input->right.y;
    if (aim_x != 0.0F || aim_y != 0.0F) {
        normalize_direction(&aim_x, &aim_y);
        game->aim_x = aim_x;
        game->aim_y = aim_y;
        game->aim_frame = aim_frame(aim_x, aim_y);
    }
    if (input->right.button_down && game->fire_cooldown == 0) {
        fire_bullet(game);
    }
}

static void update_bullets(game_t *game)
{
    for (int index = 0; index < MAX_BULLETS; ++index) {
        bullet_t *bullet = &game->bullets[index];
        if (!bullet->active) {
            continue;
        }
        bullet->x += bullet->velocity_x;
        bullet->y += bullet->velocity_y;
        if (bullet->x < -BULLET_SIZE || bullet->x >= DISPLAY_WIDTH ||
            bullet->y < PLAYFIELD_TOP - BULLET_SIZE ||
            bullet->y >= DISPLAY_HEIGHT) {
            bullet->active = false;
        }
    }
}

static void update_enemies(game_t *game)
{
    const float speed = 0.48F + (float)game->wave * 0.035F;
    for (int index = 0; index < MAX_ENEMIES; ++index) {
        enemy_t *enemy = &game->enemies[index];
        if (!enemy->active) {
            continue;
        }
        float direction_x = game->player_x - enemy->x;
        float direction_y = game->player_y - enemy->y;
        normalize_direction(&direction_x, &direction_y);
        enemy->x += direction_x * speed;
        enemy->y += direction_y * speed;
        if (enemy->flash_frames > 0) {
            --enemy->flash_frames;
        }
    }

    if (game->spawn_timer > 0) {
        --game->spawn_timer;
    } else if (active_enemy_count(game) < MAX_ENEMIES) {
        spawn_enemy(game);
        const unsigned reduction = game->wave * 2U;
        game->spawn_timer = (uint16_t)(reduction < 31U ? 34U - reduction : 3U);
    }
}

static arduboy2_rect_t rectangle(int x, int y, int width, int height)
{
    return (arduboy2_rect_t){
        .x = (int16_t)x,
        .y = (int16_t)y,
        .width = (uint8_t)width,
        .height = (uint8_t)height,
    };
}

static void resolve_collisions(game_t *game)
{
    for (int bullet_index = 0; bullet_index < MAX_BULLETS; ++bullet_index) {
        bullet_t *bullet = &game->bullets[bullet_index];
        if (!bullet->active) {
            continue;
        }
        const arduboy2_rect_t bullet_box =
            rectangle((int)bullet->x, (int)bullet->y, BULLET_SIZE, BULLET_SIZE);
        for (int enemy_index = 0; enemy_index < MAX_ENEMIES; ++enemy_index) {
            enemy_t *enemy = &game->enemies[enemy_index];
            if (!enemy->active) {
                continue;
            }
            const arduboy2_rect_t enemy_box =
                rectangle((int)enemy->x + 1, (int)enemy->y + 1,
                          ENEMY_SIZE - 2, ENEMY_SIZE - 2);
            if (!arduboy2_collide(bullet_box, enemy_box)) {
                continue;
            }
            bullet->active = false;
            enemy->flash_frames = 2;
            if (--enemy->health == 0) {
                enemy->active = false;
                ++game->kills;
                game->score += 10U + game->wave * 2U;
                game->wave = 1U + game->kills / 10U;
                if (game->kills % 7U == 0U && !game->core_active) {
                    game->core_x = enemy->x;
                    game->core_y = enemy->y;
                    game->core_active = true;
                }
            }
            break;
        }
    }

    const arduboy2_rect_t player_box =
        rectangle((int)game->player_x + 1, (int)game->player_y + 1,
                  PLAYER_SIZE - 2, PLAYER_SIZE - 2);
    if (game->core_active) {
        const arduboy2_rect_t core_box =
            rectangle((int)game->core_x, (int)game->core_y,
                      CORE_SIZE, CORE_SIZE);
        if (arduboy2_collide(player_box, core_box)) {
            game->core_active = false;
            if (game->health < STARTING_HEALTH) {
                ++game->health;
            } else {
                game->score += 25U;
            }
        }
    }
    if (game->invulnerability > 0) {
        return;
    }
    for (int index = 0; index < MAX_ENEMIES; ++index) {
        enemy_t *enemy = &game->enemies[index];
        if (!enemy->active) {
            continue;
        }
        const arduboy2_rect_t enemy_box =
            rectangle((int)enemy->x + 1, (int)enemy->y + 1,
                      ENEMY_SIZE - 2, ENEMY_SIZE - 2);
        if (!arduboy2_collide(player_box, enemy_box)) {
            continue;
        }
        enemy->active = false;
        game->invulnerability = HIT_INVULNERABILITY_FRAMES;
        if (game->health > 0) {
            --game->health;
        }
        if (game->health == 0) {
            game->screen = SCREEN_GAME_OVER;
            if (game->score > game->high_score) {
                game->high_score = game->score;
            }
            ESP_LOGI(TAG, "Game over: score=%u wave=%u", game->score, game->wave);
        }
        break;
    }
}

static void update_playing(game_t *game, const dual_joystick_state_t *input)
{
    if (game->fire_cooldown > 0) {
        --game->fire_cooldown;
    }
    if (game->dash_cooldown > 0) {
        --game->dash_cooldown;
    }
    if (game->invulnerability > 0) {
        --game->invulnerability;
    }
    update_player(game, input);
    update_bullets(game);
    update_enemies(game);
    resolve_collisions(game);
}

static void draw_starfield(uint32_t frame)
{
    for (int star = 0; star < 26; ++star) {
        const int x = (star * 53 + (int)(frame / 5U) * (star % 2 + 1)) %
                      DISPLAY_WIDTH;
        const int y = PLAYFIELD_TOP +
                      (star * 29) % (DISPLAY_HEIGHT - PLAYFIELD_TOP);
        display_draw_pixel(x, y, true);
    }
}

static void draw_hud(const game_t *game)
{
    char left_text[24];
    char right_text[24];
    snprintf(left_text, sizeof(left_text), "HP %u  SCORE %u", game->health,
             game->score);
    snprintf(right_text, sizeof(right_text), "WAVE %u  HI %u", game->wave,
             game->high_score);
    display_draw_text(1, 1, left_text, 1);
    display_draw_text(DISPLAY_PANEL_WIDTH + 2, 1, right_text, 1);
    display_fill_rectangle(0, 9, DISPLAY_WIDTH, 1);

    const int dash_width = (int)game->dash_cooldown * 22 / DASH_COOLDOWN_FRAMES;
    display_draw_rectangle(103, 1, 24, 7);
    if (dash_width == 0) {
        display_fill_rectangle(105, 3, 20, 3);
    } else {
        display_fill_rectangle(105, 3, 20 - dash_width, 3);
    }
}

static void draw_rift(const game_t *game)
{
    draw_hud(game);
    draw_starfield(arduboy2_frame_count());
    for (int y = PLAYFIELD_TOP + (int)(arduboy2_frame_count() & 3U); y < DISPLAY_HEIGHT;
         y += 7) {
        display_draw_pixel(DISPLAY_PANEL_WIDTH - 1, y, true);
        display_draw_pixel(DISPLAY_PANEL_WIDTH, y + 1, true);
    }

    for (int index = 0; index < MAX_BULLETS; ++index) {
        const bullet_t *bullet = &game->bullets[index];
        if (bullet->active) {
            display_fill_rectangle((int)bullet->x, (int)bullet->y,
                                   BULLET_SIZE, BULLET_SIZE);
        }
    }
    for (int index = 0; index < MAX_ENEMIES; ++index) {
        const enemy_t *enemy = &game->enemies[index];
        if (enemy->active && enemy->flash_frames == 0) {
            arduboy2_draw_self_masked((int16_t)enemy->x, (int16_t)enemy->y,
                                      rift_enemy_sprite,
                                      (uint8_t)((arduboy2_frame_count() / 8U) & 1U));
        }
    }
    if (game->core_active) {
        arduboy2_draw_plus_mask((int16_t)game->core_x, (int16_t)game->core_y,
                                rift_core_sprite, 0);
    }
    if (game->invulnerability == 0 || (game->invulnerability & 2U) == 0) {
        arduboy2_draw_self_masked((int16_t)game->player_x,
                                  (int16_t)game->player_y, rift_ship_sprite,
                                  game->aim_frame);
    }
}

static void draw_title(void)
{
    display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 10, "RIFT", 2);
    display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH, DISPLAY_PANEL_WIDTH,
                                         10, "RUNNER", 2);
    display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 32,
                                         "LEFT MOVE", 1);
    display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 44,
                                         "SW DASH", 1);
    display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH, DISPLAY_PANEL_WIDTH,
                                         32, "RIGHT AIM", 1);
    display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH, DISPLAY_PANEL_WIDTH,
                                         44, "SW FIRE", 1);
    display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 56,
                                         "PRESS SW", 1);
    display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH, DISPLAY_PANEL_WIDTH,
                                         56, "TO START", 1);
}

static void draw_overlay(const game_t *game)
{
    if (game->screen == SCREEN_TITLE) {
        draw_title();
        return;
    }
    if (game->screen == SCREEN_PAUSED) {
        display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 20,
                                             "PAUSED", 2);
        display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH,
                                             DISPLAY_PANEL_WIDTH, 22,
                                             "PRESS SW", 1);
        display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH,
                                             DISPLAY_PANEL_WIDTH, 36,
                                             "TO RESUME", 1);
        return;
    }
    if (game->screen == SCREEN_GAME_OVER) {
        char score_text[24];
        char high_text[24];
        snprintf(score_text, sizeof(score_text), "SCORE %u", game->score);
        snprintf(high_text, sizeof(high_text), "HIGH %u", game->high_score);
        display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 13,
                                             "GAME", 2);
        display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH,
                                             DISPLAY_PANEL_WIDTH, 13,
                                             "OVER", 2);
        display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 36,
                                             score_text, 1);
        display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH,
                                             DISPLAY_PANEL_WIDTH, 36,
                                             high_text, 1);
        display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 52,
                                             "PRESS SW", 1);
        display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH,
                                             DISPLAY_PANEL_WIDTH, 52,
                                             "TO RETRY", 1);
    }
}

static void update_screen_state(game_t *game, const dual_joystick_state_t *input)
{
    const bool any_pressed = input->left.button_pressed || input->right.button_pressed;
    if (game->screen == SCREEN_TITLE || game->screen == SCREEN_GAME_OVER) {
        if (any_pressed) {
            reset_game(game);
        }
        return;
    }
    if (game->screen == SCREEN_PAUSED) {
        if (any_pressed) {
            game->screen = SCREEN_PLAYING;
        }
        return;
    }

    if (input->left.button_down && input->right.button_down) {
        if (game->both_button_frames < PAUSE_HOLD_FRAMES) {
            ++game->both_button_frames;
        } else {
            game->screen = SCREEN_PAUSED;
            game->both_button_frames = 0;
        }
    } else {
        game->both_button_frames = 0;
    }
}

void rift_runner_run(void)
{
    game_t game = {
        .screen = SCREEN_TITLE,
        .aim_x = 1.0F,
        .health = STARTING_HEALTH,
        .wave = 1,
        .random_state = 0xC6D00D42U,
    };
    arduboy2_set_frame_rate(GAME_FRAME_RATE);
    ESP_LOGI(TAG, "Rift Runner ready at %d FPS", GAME_FRAME_RATE);

    while (true) {
        if (!arduboy2_next_frame()) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        dual_joystick_state_t input;
        joysticks_read(&input);
        const screen_state_t previous_screen = game.screen;
        update_screen_state(&game, &input);
        if (game.screen == SCREEN_PLAYING && previous_screen == SCREEN_PLAYING) {
            update_playing(&game, &input);
        }

        display_clear();
        if (game.screen == SCREEN_PLAYING) {
            draw_rift(&game);
        } else {
            draw_overlay(&game);
        }
        ESP_ERROR_CHECK(display_present());
    }
}
