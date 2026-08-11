#include "indicator.h"

#include <errno.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "canon/remote.h"
#include "controls.h"

LOG_MODULE_REGISTER(hardware_indicator, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_ALIAS(status_led), okay)

#include <zephyr/drivers/led_strip.h>

#define STATUS_LED_NODE DT_ALIAS(status_led)

#define INDICATOR_THREAD_STACK_SIZE 1536
#define INDICATOR_THREAD_PRIORITY 8
#define INDICATOR_UPDATE_MS 25
#define PAIR_HOLD_BLINK_HALF_PERIOD_MS 250
#define PAIR_ACTIVE_BLINK_HALF_PERIOD_MS 100
#define SHUTTER_FLASH_ON_MS 100
#define SHUTTER_FLASH_PERIOD_MS 200

#define PAIR_BLUE 24U
#define FOCUS_GREEN 18U
#define SHUTTER_RED 40U

typedef enum {
    INDICATOR_OFF = 0,
    INDICATOR_PAIR_HOLD,
    INDICATOR_PAIRING_ACTIVE,
    INDICATOR_FOCUS,
    INDICATOR_SHUTTER,
} indicator_mode_t;

static const struct device *const status_led = DEVICE_DT_GET(STATUS_LED_NODE);
static struct k_thread indicator_thread;
K_THREAD_STACK_DEFINE(indicator_thread_stack, INDICATOR_THREAD_STACK_SIZE);

static bool initialized;
static bool started;
static bool previous_pixel_valid;
static struct led_rgb previous_pixel;

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

static struct led_rgb mode_pixel(indicator_mode_t mode, int64_t elapsed_ms)
{
    struct led_rgb pixel = {0};

    switch (mode) {
    case INDICATOR_PAIR_HOLD:
        if ((elapsed_ms / PAIR_HOLD_BLINK_HALF_PERIOD_MS) % 2 == 0) {
            pixel.b = PAIR_BLUE;
        }
        break;
    case INDICATOR_PAIRING_ACTIVE:
        if ((elapsed_ms / PAIR_ACTIVE_BLINK_HALF_PERIOD_MS) % 2 == 0) {
            pixel.b = PAIR_BLUE;
        }
        break;
    case INDICATOR_FOCUS:
        pixel.g = FOCUS_GREEN;
        break;
    case INDICATOR_SHUTTER:
        if (elapsed_ms % SHUTTER_FLASH_PERIOD_MS < SHUTTER_FLASH_ON_MS) {
            pixel.r = SHUTTER_RED;
        }
        break;
    case INDICATOR_OFF:
        break;
    }
    return pixel;
}

static void set_pixel(struct led_rgb pixel)
{
    if (previous_pixel_valid &&
        memcmp(&pixel, &previous_pixel, sizeof(pixel)) == 0) {
        return;
    }

    const int result = led_strip_update_rgb(status_led, &pixel, 1U);
    if (result != 0) {
        LOG_WRN("Could not update status LED: %d", result);
        return;
    }
    previous_pixel = pixel;
    previous_pixel_valid = true;
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
        }
        set_pixel(mode_pixel(mode, now_ms - mode_started_ms));
        k_msleep(INDICATOR_UPDATE_MS);
    }
}

int hardware_indicator_initialize(void)
{
    if (!device_is_ready(status_led)) {
        return -ENODEV;
    }

    set_pixel((struct led_rgb){0});
    initialized = true;
    LOG_INF("Onboard RGB status LED ready");
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

#else

int hardware_indicator_initialize(void) { return 0; }

int hardware_indicator_start(void) { return 0; }

bool hardware_indicator_available(void) { return false; }

#endif
