#include "power.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/util.h>

#include "canon/remote.h"
#include "controls.h"
#include "indicator.h"

LOG_MODULE_REGISTER(hardware_power, LOG_LEVEL_INF);

#if defined(CONFIG_CANON_POWER_SAVE)

#include <esp_sleep.h>
#define FOCUS_BUTTON_NODE DT_ALIAS(focus_button)
#define SHUTTER_BUTTON_NODE DT_ALIAS(shutter_button)
#define PAIR_BUTTON_NODE DT_ALIAS(pair_button)

#define POWER_THREAD_STACK_SIZE 1536
#define POWER_THREAD_PRIORITY 9
#define POWER_CHECK_INTERVAL_MS 250

#define FOCUS_BUTTON_PIN DT_GPIO_PIN(FOCUS_BUTTON_NODE, gpios)
#define SHUTTER_BUTTON_PIN DT_GPIO_PIN(SHUTTER_BUTTON_NODE, gpios)
#define PAIR_BUTTON_PIN DT_GPIO_PIN(PAIR_BUTTON_NODE, gpios)
#define WAKE_BUTTON_MASK                                                       \
    (BIT64(FOCUS_BUTTON_PIN) | BIT64(SHUTTER_BUTTON_PIN) |                    \
     BIT64(PAIR_BUTTON_PIN))

BUILD_ASSERT(FOCUS_BUTTON_PIN <= 7, "Focus button must use an RTC GPIO");
BUILD_ASSERT(SHUTTER_BUTTON_PIN <= 7, "Shutter button must use an RTC GPIO");
BUILD_ASSERT(PAIR_BUTTON_PIN <= 7, "Pair button must use an RTC GPIO");

static struct k_thread power_thread;
K_THREAD_STACK_DEFINE(power_thread_stack, POWER_THREAD_STACK_SIZE);

static bool initialized;
static bool started;
static bool woke_from_button;

static bool activity_in_progress(void)
{
    hardware_controls_status_t controls;
    hardware_controls_get_status(&controls);

    canon_remote_status_t remote;
    canon_remote_get_status(&remote);

    return controls.focus_pressed || controls.shutter_pressed ||
           controls.pair_pressed || controls.pairing_active || remote.scanning ||
           remote.busy || remote.focus_requested || remote.shutter_requested ||
           remote.focus_applied || remote.shutter_applied;
}

static int configure_button_wakeup(void)
{
    const int result = esp_deep_sleep_enable_gpio_wakeup(
        WAKE_BUTTON_MASK, ESP_GPIO_WAKEUP_GPIO_LOW);
    return result == ESP_OK ? 0 : -EIO;
}

static void enter_deep_sleep(uint32_t expected_activity_generation)
{
    const canon_remote_result_t disconnect_result = canon_remote_disconnect();
    if (disconnect_result != CANON_REMOTE_OK) {
        LOG_WRN("Idle disconnect deferred: %s",
                canon_remote_result_name(disconnect_result));
        return;
    }
    if (activity_in_progress() ||
        hardware_controls_activity_generation() !=
            expected_activity_generation) {
        return;
    }

    const int wake_result = configure_button_wakeup();
    if (wake_result != 0) {
        LOG_ERR("Could not configure button wakeup: %d", wake_result);
        return;
    }

    LOG_INF("Idle for %d seconds; entering deep sleep",
            CONFIG_CANON_POWER_IDLE_TIMEOUT_SECONDS);
    hardware_indicator_prepare_for_sleep();
    if (activity_in_progress() ||
        hardware_controls_activity_generation() !=
            expected_activity_generation) {
        hardware_indicator_resume_after_sleep_abort();
        return;
    }

    sys_poweroff();
    CODE_UNREACHABLE;
}

static void power_thread_entry(void *first, void *second, void *third)
{
    (void)first;
    (void)second;
    (void)third;

    int64_t idle_started_ms = k_uptime_get();
    uint32_t activity_generation =
        hardware_controls_activity_generation();
    const int64_t idle_timeout_ms =
        CONFIG_CANON_POWER_IDLE_TIMEOUT_SECONDS * MSEC_PER_SEC;

    for (;;) {
        const uint32_t current_generation =
            hardware_controls_activity_generation();
        if (activity_in_progress() ||
            current_generation != activity_generation) {
            activity_generation = current_generation;
            idle_started_ms = k_uptime_get();
        } else if (k_uptime_get() - idle_started_ms >= idle_timeout_ms) {
            enter_deep_sleep(activity_generation);
            idle_started_ms = k_uptime_get();
        }
        k_msleep(POWER_CHECK_INTERVAL_MS);
    }
}

int hardware_power_initialize(void)
{
    const esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    woke_from_button = wake_cause == ESP_SLEEP_WAKEUP_GPIO;
    initialized = true;
    return 0;
}

int hardware_power_start(void)
{
    if (!initialized) {
        return -EACCES;
    }
    if (started) {
        return 0;
    }

    k_tid_t thread_id = k_thread_create(
        &power_thread, power_thread_stack,
        K_THREAD_STACK_SIZEOF(power_thread_stack), power_thread_entry, NULL,
        NULL, NULL, POWER_THREAD_PRIORITY, 0, K_NO_WAIT);
    (void)k_thread_name_set(thread_id, "idle_power");
    started = true;
    LOG_INF("Deep sleep enabled after %d idle seconds",
            CONFIG_CANON_POWER_IDLE_TIMEOUT_SECONDS);
    return 0;
}

bool hardware_power_woke_from_button(void) { return woke_from_button; }

#else

int hardware_power_initialize(void) { return 0; }

int hardware_power_start(void) { return 0; }

bool hardware_power_woke_from_button(void) { return false; }

#endif
