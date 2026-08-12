#include "indicator.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "canon/remote.h"
#include "controls.h"

LOG_MODULE_REGISTER(hardware_indicator, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_ALIAS(status_led_red), okay) &&                  \
    DT_NODE_HAS_STATUS(DT_ALIAS(status_led_green), okay) &&                \
    DT_NODE_HAS_STATUS(DT_ALIAS(status_led_blue), okay)

#include <zephyr/drivers/gpio.h>

#define INDICATOR_THREAD_STACK_SIZE 1536
#define INDICATOR_THREAD_PRIORITY 8
#define INDICATOR_UPDATE_MS 25
#define PAIR_HOLD_BLINK_HALF_PERIOD_MS 250
#define PAIR_ACTIVE_BLINK_HALF_PERIOD_MS 100
#define SHUTTER_FLASH_ON_MS 100
#define SHUTTER_FLASH_PERIOD_MS 200
#define DEEP_SLEEP_WAKE_FLASH_MS 300

#define RED_BIT (1U << 0)
#define GREEN_BIT (1U << 1)
#define BLUE_BIT (1U << 2)

typedef enum {
    INDICATOR_OFF = 0,
    INDICATOR_PAIR_HOLD,
    INDICATOR_PAIRING_ACTIVE,
    INDICATOR_FOCUS,
    INDICATOR_SHUTTER,
} indicator_mode_t;

static const struct gpio_dt_spec red_led =
    GPIO_DT_SPEC_GET(DT_ALIAS(status_led_red), gpios);
static const struct gpio_dt_spec green_led =
    GPIO_DT_SPEC_GET(DT_ALIAS(status_led_green), gpios);
static const struct gpio_dt_spec blue_led =
    GPIO_DT_SPEC_GET(DT_ALIAS(status_led_blue), gpios);

static struct k_thread indicator_thread;
K_THREAD_STACK_DEFINE(indicator_thread_stack, INDICATOR_THREAD_STACK_SIZE);

static bool initialized;
static bool started;
static bool previous_output_valid;
static uint8_t previous_output;

static indicator_mode_t select_mode(void)
{
    hardware_controls_status_t controls;
    hardware_controls_get_status(&controls);

    canon_remote_status_t remote;
    canon_remote_get_status(&remote);

    if (controls.pairing_active || remote.scanning) {
        return INDICATOR_PAIRING_ACTIVE;
    }
    if (controls.pair_pressed) {
        return INDICATOR_PAIR_HOLD;
    }
    if (controls.shutter_pressed) {
        return INDICATOR_SHUTTER;
    }
    if (controls.focus_pressed) {
        return INDICATOR_FOCUS;
    }
    return INDICATOR_OFF;
}

static uint8_t mode_output(indicator_mode_t mode, int64_t elapsed_ms)
{
    switch (mode) {
    case INDICATOR_PAIR_HOLD:
        return (elapsed_ms / PAIR_HOLD_BLINK_HALF_PERIOD_MS) % 2 == 0
                   ? BLUE_BIT
                   : 0U;
    case INDICATOR_PAIRING_ACTIVE:
        return (elapsed_ms / PAIR_ACTIVE_BLINK_HALF_PERIOD_MS) % 2 == 0
                   ? BLUE_BIT
                   : 0U;
    case INDICATOR_FOCUS:
        return GREEN_BIT;
    case INDICATOR_SHUTTER:
        return elapsed_ms % SHUTTER_FLASH_PERIOD_MS < SHUTTER_FLASH_ON_MS
                   ? RED_BIT
                   : 0U;
    case INDICATOR_OFF:
        return 0U;
    }
    return 0U;
}

static int set_output(uint8_t output)
{
    if (previous_output_valid && output == previous_output) {
        return 0;
    }

    int result = gpio_pin_set_dt(&red_led, (output & RED_BIT) != 0U);
    if (result == 0) {
        result = gpio_pin_set_dt(&green_led, (output & GREEN_BIT) != 0U);
    }
    if (result == 0) {
        result = gpio_pin_set_dt(&blue_led, (output & BLUE_BIT) != 0U);
    }
    if (result != 0) {
        LOG_WRN("Could not update status LED: %d", result);
        previous_output_valid = false;
        return result;
    }

    previous_output = output;
    previous_output_valid = true;
    return 0;
}

static void indicator_thread_entry(void *first, void *second, void *third)
{
    (void)first;
    (void)second;
    (void)third;

    indicator_mode_t previous_mode = INDICATOR_OFF;
    int64_t mode_started_ms = k_uptime_get();

    for (;;) {
        const indicator_mode_t mode = select_mode();
        const int64_t now_ms = k_uptime_get();
        if (mode != previous_mode) {
            previous_mode = mode;
            mode_started_ms = now_ms;
            previous_output_valid = false;
        }
        (void)set_output(mode_output(mode, now_ms - mode_started_ms));
        k_msleep(INDICATOR_UPDATE_MS);
    }
}

int hardware_indicator_initialize(void)
{
    if (!gpio_is_ready_dt(&red_led) || !gpio_is_ready_dt(&green_led) ||
        !gpio_is_ready_dt(&blue_led)) {
        return -ENODEV;
    }

    int result = gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_INACTIVE);
    if (result == 0) {
        result = gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_INACTIVE);
    }
    if (result == 0) {
        result = gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT_INACTIVE);
    }
    if (result != 0) {
        return result;
    }

    previous_output = 0U;
    previous_output_valid = true;
    initialized = true;
    LOG_INF("External RGB status LED ready");
    return 0;
}

int hardware_indicator_start(void)
{
    if (!initialized) {
        return -EACCES;
    }
    if (started) {
        return 0;
    }

    k_tid_t thread_id = k_thread_create(
        &indicator_thread, indicator_thread_stack,
        K_THREAD_STACK_SIZEOF(indicator_thread_stack), indicator_thread_entry,
        NULL, NULL, NULL, INDICATOR_THREAD_PRIORITY, 0, K_NO_WAIT);
    (void)k_thread_name_set(thread_id, "status_rgb");
    started = true;
    return 0;
}

bool hardware_indicator_available(void) { return true; }

void hardware_indicator_show_deep_sleep_wake(void)
{
    if (!initialized) {
        return;
    }

    previous_output_valid = false;
    (void)set_output(RED_BIT | GREEN_BIT | BLUE_BIT);
    k_msleep(DEEP_SLEEP_WAKE_FLASH_MS);
    (void)set_output(0U);
}

void hardware_indicator_prepare_for_sleep(void)
{
    if (started) {
        k_thread_suspend(&indicator_thread);
    }
    if (initialized) {
        previous_output_valid = false;
        (void)set_output(0U);
    }
}

void hardware_indicator_resume_after_sleep_abort(void)
{
    if (started) {
        previous_output_valid = false;
        k_thread_resume(&indicator_thread);
    }
}

#else

int hardware_indicator_initialize(void) { return 0; }

int hardware_indicator_start(void) { return 0; }

void hardware_indicator_show_deep_sleep_wake(void) {}

void hardware_indicator_prepare_for_sleep(void) {}

void hardware_indicator_resume_after_sleep_abort(void) {}

bool hardware_indicator_available(void) { return false; }

#endif
