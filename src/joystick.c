#include "joystick.h"

#include <stdint.h>

#include "display_config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ADC_MAX_VALUE 4095
#define JOYSTICK_COUNT 2
#define AXIS_COUNT 2

typedef struct {
    adc_channel_t x_channel;
    adc_channel_t y_channel;
    gpio_num_t button_gpio;
    bool invert_x;
    bool invert_y;
    const char *name;
} joystick_config_t;

static const joystick_config_t joystick_configs[JOYSTICK_COUNT] = {
    {
        .x_channel = JOYSTICK_LEFT_X_ADC_CHANNEL,
        .y_channel = JOYSTICK_LEFT_Y_ADC_CHANNEL,
        .button_gpio = JOYSTICK_LEFT_SW_GPIO,
        .invert_x = JOYSTICK_LEFT_INVERT_X,
        .invert_y = JOYSTICK_LEFT_INVERT_Y,
        .name = "left",
    },
    {
        .x_channel = JOYSTICK_RIGHT_X_ADC_CHANNEL,
        .y_channel = JOYSTICK_RIGHT_Y_ADC_CHANNEL,
        .button_gpio = JOYSTICK_RIGHT_SW_GPIO,
        .invert_x = JOYSTICK_RIGHT_INVERT_X,
        .invert_y = JOYSTICK_RIGHT_INVERT_Y,
        .name = "right",
    },
};

static const char *TAG = "joysticks";
static adc_oneshot_unit_handle_t adc_handle;
static int centers[JOYSTICK_COUNT][AXIS_COUNT];
static bool previous_button_down[JOYSTICK_COUNT];

esp_err_t joysticks_initialize(void)
{
    const adc_oneshot_unit_init_cfg_t unit_configuration = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_configuration, &adc_handle), TAG,
                        "Could not initialize ADC1");

    const adc_oneshot_chan_cfg_t channel_configuration = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    for (int index = 0; index < JOYSTICK_COUNT; ++index) {
        ESP_RETURN_ON_ERROR(
            adc_oneshot_config_channel(adc_handle, joystick_configs[index].x_channel,
                                       &channel_configuration),
            TAG, "Could not configure %s joystick X", joystick_configs[index].name);
        ESP_RETURN_ON_ERROR(
            adc_oneshot_config_channel(adc_handle, joystick_configs[index].y_channel,
                                       &channel_configuration),
            TAG, "Could not configure %s joystick Y", joystick_configs[index].name);
    }

    const gpio_config_t button_configuration = {
        .pin_bit_mask = (1ULL << JOYSTICK_LEFT_SW_GPIO) |
                        (1ULL << JOYSTICK_RIGHT_SW_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&button_configuration), TAG,
                        "Could not configure joystick switches");
    for (int index = 0; index < JOYSTICK_COUNT; ++index) {
        previous_button_down[index] =
            gpio_get_level(joystick_configs[index].button_gpio) == 0;
    }
    return ESP_OK;
}

void joysticks_calibrate(void)
{
    int32_t totals[JOYSTICK_COUNT][AXIS_COUNT] = {{0}};
    for (int sample = 0; sample < JOYSTICK_CALIBRATION_SAMPLES; ++sample) {
        for (int index = 0; index < JOYSTICK_COUNT; ++index) {
            int raw_x = 0;
            int raw_y = 0;
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle,
                                             joystick_configs[index].x_channel, &raw_x));
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle,
                                             joystick_configs[index].y_channel, &raw_y));
            totals[index][0] += raw_x;
            totals[index][1] += raw_y;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    for (int index = 0; index < JOYSTICK_COUNT; ++index) {
        centers[index][0] = totals[index][0] / JOYSTICK_CALIBRATION_SAMPLES;
        centers[index][1] = totals[index][1] / JOYSTICK_CALIBRATION_SAMPLES;
        ESP_LOGI(TAG, "%s center: x=%d, y=%d", joystick_configs[index].name,
                 centers[index][0], centers[index][1]);
    }
}

static float normalized_axis(int raw_value, int center_value, bool inverted)
{
    int delta = raw_value - center_value;
    if (inverted) {
        delta = -delta;
    }
    const int magnitude = delta < 0 ? -delta : delta;
    if (magnitude <= JOYSTICK_CAMERA_DEAD_ZONE) {
        return 0.0F;
    }

    int available_range = delta < 0 ? center_value : ADC_MAX_VALUE - center_value;
    available_range -= JOYSTICK_CAMERA_DEAD_ZONE;
    if (available_range <= 0) {
        return 0.0F;
    }
    float value = (float)(magnitude - JOYSTICK_CAMERA_DEAD_ZONE) /
                  (float)available_range;
    if (value > 1.0F) {
        value = 1.0F;
    }
    return delta < 0 ? -value : value;
}

static void read_joystick(int index, joystick_state_t *state)
{
    int raw_x = 0;
    int raw_y = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, joystick_configs[index].x_channel,
                                     &raw_x));
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, joystick_configs[index].y_channel,
                                     &raw_y));
    state->x = normalized_axis(raw_x, centers[index][0],
                               joystick_configs[index].invert_x);
    state->y = normalized_axis(raw_y, centers[index][1],
                               joystick_configs[index].invert_y);

    state->button_down = gpio_get_level(joystick_configs[index].button_gpio) == 0;
    state->button_pressed = state->button_down && !previous_button_down[index];
    state->button_released = !state->button_down && previous_button_down[index];
    previous_button_down[index] = state->button_down;
}

void joysticks_read(dual_joystick_state_t *state)
{
    read_joystick(0, &state->left);
    read_joystick(1, &state->right);
}
