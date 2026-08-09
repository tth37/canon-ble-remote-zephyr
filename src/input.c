#include "input.h"

#include <stdbool.h>
#include <stdint.h>

#include "display_config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"

#define BUTTON_DEBOUNCE_MS 40
#define BUTTON_LONG_PRESS_MS 900

static const char *TAG = "input";
static adc_oneshot_unit_handle_t adc_handle;
static int joystick_center_x;
static int joystick_center_y;
static input_direction_t latched_direction;
static bool button_last_raw_pressed;
static bool button_stable_pressed;
static bool button_long_reported;
static TickType_t button_raw_changed_at;
static TickType_t button_pressed_at;

esp_err_t input_initialize(void)
{
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_config, &adc_handle), TAG,
                        "Could not initialize ADC1");

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(adc_handle, JOYSTICK_X_ADC_CHANNEL,
                                                   &channel_config),
                        TAG, "Could not configure joystick X");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(adc_handle, JOYSTICK_Y_ADC_CHANNEL,
                                                   &channel_config),
                        TAG, "Could not configure joystick Y");

    const gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << JOYSTICK_SW_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&button_config), TAG, "Could not configure joystick SW");

    button_last_raw_pressed = gpio_get_level(JOYSTICK_SW_GPIO) == 0;
    button_stable_pressed = button_last_raw_pressed;
    button_raw_changed_at = xTaskGetTickCount();
    button_pressed_at = button_raw_changed_at;
    return ESP_OK;
}

void input_calibrate_joystick(void)
{
    int32_t total_x = 0;
    int32_t total_y = 0;
    for (int sample = 0; sample < JOYSTICK_CALIBRATION_SAMPLES; ++sample) {
        int raw_x = 0;
        int raw_y = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, JOYSTICK_X_ADC_CHANNEL, &raw_x));
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, JOYSTICK_Y_ADC_CHANNEL, &raw_y));
        total_x += raw_x;
        total_y += raw_y;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    joystick_center_x = (int)(total_x / JOYSTICK_CALIBRATION_SAMPLES);
    joystick_center_y = (int)(total_y / JOYSTICK_CALIBRATION_SAMPLES);
    latched_direction = INPUT_DIRECTION_NONE;
    ESP_LOGI(TAG, "Joystick center: x=%d, y=%d", joystick_center_x, joystick_center_y);
}

static const char *direction_name(input_direction_t direction)
{
    switch (direction) {
        case INPUT_DIRECTION_UP: return "UP";
        case INPUT_DIRECTION_DOWN: return "DOWN";
        case INPUT_DIRECTION_LEFT: return "LEFT";
        case INPUT_DIRECTION_RIGHT: return "RIGHT";
        default: return "CENTER";
    }
}

input_direction_t input_read_direction(void)
{
    int raw_x = 0;
    int raw_y = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, JOYSTICK_X_ADC_CHANNEL, &raw_x));
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, JOYSTICK_Y_ADC_CHANNEL, &raw_y));

    int delta_x = raw_x - joystick_center_x;
    int delta_y = raw_y - joystick_center_y;
    if (JOYSTICK_INVERT_X) {
        delta_x = -delta_x;
    }
    if (JOYSTICK_INVERT_Y) {
        delta_y = -delta_y;
    }

    const int magnitude_x = delta_x < 0 ? -delta_x : delta_x;
    const int magnitude_y = delta_y < 0 ? -delta_y : delta_y;
    input_direction_t direction = INPUT_DIRECTION_NONE;
    if (magnitude_x > JOYSTICK_DEAD_ZONE || magnitude_y > JOYSTICK_DEAD_ZONE) {
        if (magnitude_x >= magnitude_y) {
            direction = delta_x < 0 ? INPUT_DIRECTION_LEFT : INPUT_DIRECTION_RIGHT;
        } else {
            direction = delta_y < 0 ? INPUT_DIRECTION_UP : INPUT_DIRECTION_DOWN;
        }
    }

    if (direction == INPUT_DIRECTION_NONE) {
        latched_direction = INPUT_DIRECTION_NONE;
        return INPUT_DIRECTION_NONE;
    }
    if (latched_direction != INPUT_DIRECTION_NONE) {
        return INPUT_DIRECTION_NONE;
    }

    latched_direction = direction;
    ESP_LOGI(TAG, "Joystick %s (x=%d, y=%d)", direction_name(direction), raw_x, raw_y);
    return direction;
}

input_button_event_t input_read_button(TickType_t now)
{
    const bool raw_pressed = gpio_get_level(JOYSTICK_SW_GPIO) == 0;
    if (raw_pressed != button_last_raw_pressed) {
        button_last_raw_pressed = raw_pressed;
        button_raw_changed_at = now;
    }

    if (raw_pressed != button_stable_pressed &&
        now - button_raw_changed_at >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
        button_stable_pressed = raw_pressed;
        if (raw_pressed) {
            button_pressed_at = now;
            button_long_reported = false;
            return INPUT_BUTTON_PRESSED;
        }
    }

    if (button_stable_pressed && !button_long_reported &&
        now - button_pressed_at >= pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS)) {
        button_long_reported = true;
        ESP_LOGI(TAG, "Joystick SW long press");
        return INPUT_BUTTON_LONG_PRESS;
    }
    return INPUT_BUTTON_NONE;
}
