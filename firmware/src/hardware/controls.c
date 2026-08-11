#include "controls.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "canon/remote.h"

LOG_MODULE_REGISTER(hardware_controls, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_ALIAS(focus_button), okay) &&                        \
    DT_NODE_HAS_STATUS(DT_ALIAS(shutter_button), okay) &&                      \
    DT_NODE_HAS_STATUS(DT_ALIAS(pair_button), okay)

#include <zephyr/drivers/gpio.h>

#define FOCUS_BUTTON_NODE DT_ALIAS(focus_button)
#define SHUTTER_BUTTON_NODE DT_ALIAS(shutter_button)
#define PAIR_BUTTON_NODE DT_ALIAS(pair_button)

#define PAIR_HOLD_MS 5000U
#define PAIR_START_WAIT_MS 6000U
#define PAIR_RETRY_MS 20U
#define DEBOUNCE_QUIET_MS 8U
#define BUTTON_THREAD_STACK_SIZE 4096
#define BUTTON_THREAD_PRIORITY 6

#define PHYSICAL_FOCUS_BIT 0
#define PHYSICAL_SHUTTER_BIT 1
#define PHYSICAL_PAIR_BIT 2

typedef enum {
    CONTROLS_ACTIVE = 0,
    CONTROLS_PAIR_HOLD,
    CONTROLS_WAIT_RELEASE,
} controls_mode_t;

static const struct gpio_dt_spec focus_button =
    GPIO_DT_SPEC_GET(FOCUS_BUTTON_NODE, gpios);
static const struct gpio_dt_spec shutter_button =
    GPIO_DT_SPEC_GET(SHUTTER_BUTTON_NODE, gpios);
static const struct gpio_dt_spec pair_button =
    GPIO_DT_SPEC_GET(PAIR_BUTTON_NODE, gpios);

static struct gpio_callback focus_callback;
static struct gpio_callback shutter_callback;
static struct gpio_callback pair_callback;
static struct k_thread button_thread;
K_THREAD_STACK_DEFINE(hardware_button_thread_stack, BUTTON_THREAD_STACK_SIZE);
K_SEM_DEFINE(button_edge, 0, 1);

static atomic_t physical_buttons;
static atomic_t pair_hold_remaining_ms;
static atomic_t pairing_active;
static atomic_t pair_cancel_requested;
static bool initialized;
static bool started;
static bool forwarded_focus;
static bool forwarded_shutter;
static controls_mode_t controls_mode;
static int64_t pair_hold_deadline_ms;

static void pair_cancel_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(pair_cancel_work, pair_cancel_work_handler);

static uint8_t button_mask(bool focus_pressed, bool shutter_pressed,
                           bool pair_pressed)
{
    return (focus_pressed ? BIT(PHYSICAL_FOCUS_BIT) : 0U) |
           (shutter_pressed ? BIT(PHYSICAL_SHUTTER_BIT) : 0U) |
           (pair_pressed ? BIT(PHYSICAL_PAIR_BIT) : 0U);
}

static void publish_button_status(bool focus_pressed, bool shutter_pressed,
                                  bool pair_pressed)
{
    atomic_set(&physical_buttons,
               button_mask(focus_pressed, shutter_pressed, pair_pressed));
}

static int read_buttons(bool *focus_pressed, bool *shutter_pressed,
                        bool *pair_pressed)
{
    const int focus = gpio_pin_get_dt(&focus_button);
    if (focus < 0) {
        return focus;
    }
    const int shutter = gpio_pin_get_dt(&shutter_button);
    if (shutter < 0) {
        return shutter;
    }
    const int pair = gpio_pin_get_dt(&pair_button);
    if (pair < 0) {
        return pair;
    }

    *focus_pressed = focus != 0;
    *shutter_pressed = shutter != 0;
    *pair_pressed = pair != 0;
    publish_button_status(*focus_pressed, *shutter_pressed, *pair_pressed);
    return 0;
}

static void button_edge_callback(const struct device *port,
                                 struct gpio_callback *callback,
                                 gpio_port_pins_t pins)
{
    (void)port;
    (void)callback;
    (void)pins;
    k_sem_give(&button_edge);
    if (atomic_get(&pairing_active)) {
        (void)k_work_reschedule(&pair_cancel_work, K_MSEC(DEBOUNCE_QUIET_MS));
    }
}

static void pair_cancel_work_handler(struct k_work *work)
{
    (void)work;
    if (!atomic_get(&pairing_active)) {
        return;
    }

    bool focus_pressed;
    bool shutter_pressed;
    bool pair_pressed;
    const int result =
        read_buttons(&focus_pressed, &shutter_pressed, &pair_pressed);
    if (result != 0) {
        LOG_WRN("Could not read pairing-cancel buttons: %d", result);
        return;
    }
    if (!focus_pressed && !shutter_pressed && !pair_pressed) {
        return;
    }
    if (!atomic_cas(&pair_cancel_requested, 0, 1)) {
        return;
    }

    const canon_remote_result_t cancel_result = canon_remote_cancel_pairing();
    if (cancel_result == CANON_REMOTE_OK) {
        LOG_INF("Pairing cancelled by camera button");
    } else {
        LOG_WRN("Could not cancel pairing: %s",
                canon_remote_result_name(cancel_result));
    }
}

static void wait_for_quiet_inputs(void)
{
    do {
        k_msleep(DEBOUNCE_QUIET_MS);
    } while (k_sem_take(&button_edge, K_NO_WAIT) == 0);
}

static void apply_button(canon_remote_button_t button, bool pressed,
                         const char *name)
{
    const canon_remote_result_t result =
        canon_remote_set_button(button, pressed);
    if (result != CANON_REMOTE_OK) {
        LOG_WRN("Could not queue %s %s: %s", name,
                pressed ? "press" : "release",
                canon_remote_result_name(result));
    }
}

static void forward_button_state(bool focus_pressed, bool shutter_pressed)
{
    if (focus_pressed != forwarded_focus) {
        forwarded_focus = focus_pressed;
        apply_button(CANON_REMOTE_BUTTON_FOCUS, focus_pressed, "focus");
    }
    if (shutter_pressed != forwarded_shutter) {
        forwarded_shutter = shutter_pressed;
        apply_button(CANON_REMOTE_BUTTON_SHUTTER, shutter_pressed, "shutter");
    }
}

static void enter_active(void)
{
    controls_mode = CONTROLS_ACTIVE;
    pair_hold_deadline_ms = 0;
    atomic_set(&pair_hold_remaining_ms, 0);
    atomic_set(&pairing_active, 0);
}

static void enter_pair_hold(int64_t now_ms)
{
    forward_button_state(false, false);
    controls_mode = CONTROLS_PAIR_HOLD;
    pair_hold_deadline_ms = now_ms + PAIR_HOLD_MS;
    atomic_set(&pair_hold_remaining_ms, PAIR_HOLD_MS);
    LOG_INF("Pair button pressed; hold for five seconds");
}

static void run_pairing(void)
{
    atomic_set(&pair_hold_remaining_ms, 0);
    atomic_set(&pair_cancel_requested, 0);
    atomic_set(&pairing_active, 1);
    LOG_INF("Pair button accepted; scanning for %u seconds",
            HARDWARE_PAIR_SCAN_SECONDS);

    canon_remote_result_t result = CANON_REMOTE_BUSY;
    const int64_t start_deadline_ms = k_uptime_get() + PAIR_START_WAIT_MS;
    do {
        if (atomic_get(&pair_cancel_requested)) {
            result = CANON_REMOTE_CANCELLED;
            break;
        }
        result = canon_remote_pair(HARDWARE_PAIR_SCAN_SECONDS);
        if (result != CANON_REMOTE_BUSY) {
            break;
        }
        k_msleep(PAIR_RETRY_MS);
    } while (k_uptime_get() < start_deadline_ms);

    if (result == CANON_REMOTE_OK) {
        LOG_INF("Pair-button pairing succeeded");
    } else if (result == CANON_REMOTE_CANCELLED) {
        LOG_INF("Pair-button pairing cancelled");
    } else {
        LOG_WRN("Pair-button pairing failed: %s",
                canon_remote_result_name(result));
    }
    atomic_set(&pairing_active, 0);
    controls_mode = CONTROLS_WAIT_RELEASE;
}

static void process_button_state(bool focus_pressed, bool shutter_pressed,
                                 bool pair_pressed)
{
    const int64_t now_ms = k_uptime_get();

    switch (controls_mode) {
    case CONTROLS_ACTIVE:
        if (pair_pressed) {
            enter_pair_hold(now_ms);
        } else {
            forward_button_state(focus_pressed, shutter_pressed);
        }
        break;

    case CONTROLS_PAIR_HOLD:
        if (!pair_pressed) {
            atomic_set(&pair_hold_remaining_ms, 0);
            if (!focus_pressed && !shutter_pressed) {
                enter_active();
                LOG_INF("Pairing request cancelled; camera buttons armed");
            } else {
                controls_mode = CONTROLS_WAIT_RELEASE;
                LOG_INF("Pairing request cancelled; release all buttons");
            }
        } else if (now_ms >= pair_hold_deadline_ms) {
            run_pairing();
        } else {
            atomic_set(&pair_hold_remaining_ms,
                       (atomic_val_t)(pair_hold_deadline_ms - now_ms));
        }
        break;

    case CONTROLS_WAIT_RELEASE:
        if (!focus_pressed && !shutter_pressed && !pair_pressed) {
            forwarded_focus = false;
            forwarded_shutter = false;
            enter_active();
            LOG_INF("Camera buttons armed");
        }
        break;
    }
}

static void button_thread_entry(void *first, void *second, void *third)
{
    (void)first;
    (void)second;
    (void)third;

    for (;;) {
        k_timeout_t timeout = K_FOREVER;
        if (controls_mode == CONTROLS_PAIR_HOLD) {
            const int64_t remaining_ms = pair_hold_deadline_ms - k_uptime_get();
            timeout = remaining_ms <= 0 ? K_NO_WAIT : K_MSEC(remaining_ms);
        }

        if (k_sem_take(&button_edge, timeout) == 0) {
            wait_for_quiet_inputs();
        }

        bool focus_pressed;
        bool shutter_pressed;
        bool pair_pressed;
        const int result =
            read_buttons(&focus_pressed, &shutter_pressed, &pair_pressed);
        if (result != 0) {
            LOG_WRN("Could not read camera buttons: %d", result);
            continue;
        }
        process_button_state(focus_pressed, shutter_pressed, pair_pressed);
    }
}

int hardware_controls_initialize(void)
{
    if (!device_is_ready(focus_button.port) ||
        !device_is_ready(shutter_button.port) ||
        !device_is_ready(pair_button.port)) {
        return -ENODEV;
    }

    int result = gpio_pin_configure_dt(&focus_button, GPIO_INPUT);
    if (result != 0) {
        return result;
    }
    result = gpio_pin_configure_dt(&shutter_button, GPIO_INPUT);
    if (result != 0) {
        return result;
    }
    result = gpio_pin_configure_dt(&pair_button, GPIO_INPUT);
    if (result != 0) {
        return result;
    }

    bool focus_pressed;
    bool shutter_pressed;
    bool pair_pressed;
    result = read_buttons(&focus_pressed, &shutter_pressed, &pair_pressed);
    if (result != 0) {
        return result;
    }

    initialized = true;
    return 0;
}

int hardware_controls_start(void)
{
    if (!initialized) {
        return -EACCES;
    }
    if (started) {
        return 0;
    }

    gpio_init_callback(&focus_callback, button_edge_callback,
                       BIT(focus_button.pin));
    gpio_init_callback(&shutter_callback, button_edge_callback,
                       BIT(shutter_button.pin));
    gpio_init_callback(&pair_callback, button_edge_callback,
                       BIT(pair_button.pin));

    int result = gpio_add_callback(focus_button.port, &focus_callback);
    if (result != 0) {
        return result;
    }
    result = gpio_add_callback(shutter_button.port, &shutter_callback);
    if (result != 0) {
        (void)gpio_remove_callback(focus_button.port, &focus_callback);
        return result;
    }
    result = gpio_add_callback(pair_button.port, &pair_callback);
    if (result != 0) {
        (void)gpio_remove_callback(shutter_button.port, &shutter_callback);
        (void)gpio_remove_callback(focus_button.port, &focus_callback);
        return result;
    }

    result = gpio_pin_interrupt_configure_dt(&focus_button, GPIO_INT_EDGE_BOTH);
    if (result != 0) {
        goto remove_callbacks;
    }
    result =
        gpio_pin_interrupt_configure_dt(&shutter_button, GPIO_INT_EDGE_BOTH);
    if (result != 0) {
        (void)gpio_pin_interrupt_configure_dt(&focus_button, GPIO_INT_DISABLE);
        goto remove_callbacks;
    }
    result = gpio_pin_interrupt_configure_dt(&pair_button, GPIO_INT_EDGE_BOTH);
    if (result != 0) {
        (void)gpio_pin_interrupt_configure_dt(&shutter_button,
                                              GPIO_INT_DISABLE);
        (void)gpio_pin_interrupt_configure_dt(&focus_button, GPIO_INT_DISABLE);
        goto remove_callbacks;
    }

    bool focus_pressed;
    bool shutter_pressed;
    bool pair_pressed;
    result = read_buttons(&focus_pressed, &shutter_pressed, &pair_pressed);
    if (result != 0) {
        goto disable_interrupts;
    }

    forwarded_focus = false;
    forwarded_shutter = false;
    if (pair_pressed) {
        enter_pair_hold(k_uptime_get());
    } else if (focus_pressed || shutter_pressed) {
        controls_mode = CONTROLS_WAIT_RELEASE;
        atomic_set(&pair_hold_remaining_ms, 0);
        atomic_set(&pairing_active, 0);
    } else {
        enter_active();
    }

    k_tid_t thread_id =
        k_thread_create(&button_thread, hardware_button_thread_stack,
                        K_THREAD_STACK_SIZEOF(hardware_button_thread_stack),
                        button_thread_entry, NULL, NULL, NULL,
                        BUTTON_THREAD_PRIORITY, 0, K_NO_WAIT);
    (void)k_thread_name_set(thread_id, "camera_inputs");
    started = true;
    LOG_INF("Camera buttons ready%s", controls_mode == CONTROLS_ACTIVE
                                          ? ""
                                          : "; release all buttons to arm");
    return 0;

disable_interrupts:
    (void)gpio_pin_interrupt_configure_dt(&pair_button, GPIO_INT_DISABLE);
    (void)gpio_pin_interrupt_configure_dt(&shutter_button, GPIO_INT_DISABLE);
    (void)gpio_pin_interrupt_configure_dt(&focus_button, GPIO_INT_DISABLE);
remove_callbacks:
    (void)gpio_remove_callback(pair_button.port, &pair_callback);
    (void)gpio_remove_callback(shutter_button.port, &shutter_callback);
    (void)gpio_remove_callback(focus_button.port, &focus_callback);
    return result;
}

void hardware_controls_get_status(hardware_controls_status_t *status)
{
    if (status == NULL) {
        return;
    }
    const atomic_val_t buttons = atomic_get(&physical_buttons);
    status->focus_pressed = (buttons & BIT(PHYSICAL_FOCUS_BIT)) != 0;
    status->shutter_pressed = (buttons & BIT(PHYSICAL_SHUTTER_BIT)) != 0;
    status->pair_pressed = (buttons & BIT(PHYSICAL_PAIR_BIT)) != 0;
    status->pairing_active = atomic_get(&pairing_active) != 0;
    status->pair_hold_remaining_ms =
        (uint32_t)atomic_get(&pair_hold_remaining_ms);
}

bool hardware_controls_available(void) { return true; }

#else

int hardware_controls_initialize(void) { return 0; }

int hardware_controls_start(void) { return 0; }

void hardware_controls_get_status(hardware_controls_status_t *status)
{
    if (status == NULL) {
        return;
    }
    status->focus_pressed = false;
    status->shutter_pressed = false;
    status->pair_pressed = false;
    status->pairing_active = false;
    status->pair_hold_remaining_ms = 0U;
}

bool hardware_controls_available(void) { return false; }

#endif
