#include "display.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "canon/remote.h"
#include "controls.h"

LOG_MODULE_REGISTER(hardware_display, LOG_LEVEL_INF);

#if DT_HAS_CHOSEN(zephyr_display) &&                                           \
    DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_display), okay)

#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>

#include "font_5x7.h"

#define DISPLAY_THREAD_STACK_SIZE 2048
#define DISPLAY_THREAD_PRIORITY 7
#define DISPLAY_REFRESH_MS 100
#define SCREEN_ROWS 4
#define SCREEN_COLUMNS 20
#define STATUS_FONT_WIDTH 6
#define STATUS_FONT_HEIGHT 8
#define DISPLAY_LINE_HEIGHT 16
#define DISPLAY_HORIZONTAL_PADDING 4
#define DISPLAY_VERTICAL_PADDING 4
#define FONT_FIRST_CHARACTER 32
#define FONT_LAST_CHARACTER 126
#define FONT_CHARACTER_COUNT (FONT_LAST_CHARACTER - FONT_FIRST_CHARACTER + 1)

static uint8_t status_font_68[FONT_CHARACTER_COUNT][STATUS_FONT_WIDTH];
FONT_ENTRY_DEFINE(status_font, STATUS_FONT_WIDTH, STATUS_FONT_HEIGHT,
                  CFB_FONT_MONO_VPACKED, status_font_68, FONT_FIRST_CHARACTER,
                  FONT_LAST_CHARACTER);

static const struct device *const display =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static struct k_thread display_thread;
K_THREAD_STACK_DEFINE(display_thread_stack, DISPLAY_THREAD_STACK_SIZE);

static bool initialized;
static bool started;
static char previous_screen[SCREEN_ROWS][SCREEN_COLUMNS + 1];

static void prepare_status_font(void)
{
    memset(status_font_68, 0, sizeof(status_font_68));
    for (size_t glyph = 0; glyph < FONT_CHARACTER_COUNT; ++glyph) {
        memcpy(status_font_68[glyph], hardware_font_5x7_columns[glyph], 5U);
    }
}

static int select_status_font(void)
{
    for (int index = 0; index < 16; ++index) {
        uint8_t width;
        uint8_t height;
        if (cfb_get_font_size(display, index, &width, &height) != 0) {
            break;
        }
        if (width == STATUS_FONT_WIDTH && height == STATUS_FONT_HEIGHT) {
            return cfb_framebuffer_set_font(display, index);
        }
    }
    return -ENOTSUP;
}

static int draw_screen(const char screen[SCREEN_ROWS][SCREEN_COLUMNS + 1],
                       bool force)
{
    if (!force &&
        memcmp(screen, previous_screen, sizeof(previous_screen)) == 0) {
        return 0;
    }

    int result = cfb_framebuffer_clear(display, false);
    if (result != 0) {
        return result;
    }

    const struct cfb_position border_start = {.x = 0U, .y = 0U};
    const struct cfb_position border_end = {
        .x = (uint16_t)(cfb_get_display_parameter(display, CFB_DISPLAY_WIDTH) -
                        1),
        .y = (uint16_t)(cfb_get_display_parameter(display, CFB_DISPLAY_HEIGHT) -
                        1),
    };
    result = cfb_draw_rect(display, &border_start, &border_end);
    if (result != 0) {
        return result;
    }

    for (size_t row = 0; row < SCREEN_ROWS; ++row) {
        result = cfb_print(
            display, screen[row], DISPLAY_HORIZONTAL_PADDING,
            (uint16_t)(DISPLAY_VERTICAL_PADDING + row * DISPLAY_LINE_HEIGHT));
        if (result != 0) {
            return result;
        }
    }
    result = cfb_framebuffer_finalize(display);
    if (result == 0) {
        memcpy(previous_screen, screen, sizeof(previous_screen));
    }
    return result;
}

static void build_status_screen(char screen[SCREEN_ROWS][SCREEN_COLUMNS + 1])
{
    canon_remote_status_t status;
    canon_remote_get_status(&status);
    hardware_controls_status_t controls;
    hardware_controls_get_status(&controls);

    (void)snprintf(screen[0], sizeof(screen[0]), "%-20.20s",
                   CONFIG_BT_DEVICE_NAME);

    const char *camera_state;
    if (controls.pair_hold_remaining_ms > 0U) {
        camera_state = "PAIR: KEEP HOLDING";
    } else if (controls.pairing_active && !status.scanning) {
        camera_state = "PAIR: STARTING";
    } else if (!status.initialized || !status.host_ready) {
        camera_state = "BLE OFFLINE";
    } else if (status.scanning) {
        camera_state = "PAIRING...";
    } else if (status.busy && !status.ready) {
        camera_state = "CONNECTING";
    } else if (status.ready) {
        camera_state = "CAMERA READY";
    } else if (status.paired) {
        camera_state = "CAM STANDBY";
    } else {
        camera_state = "NO CAMERA";
    }
    (void)snprintf(screen[1], sizeof(screen[1]), "%-20.20s", camera_state);
    (void)snprintf(screen[2], sizeof(screen[2]), "F:%-4s S:%-4s P:%-4s",
                   controls.focus_pressed ? "DOWN" : "--",
                   controls.shutter_pressed ? "DOWN" : "--",
                   controls.pair_pressed ? "DOWN" : "--");

    const bool sending = status.focus_requested != status.focus_applied ||
                         status.shutter_requested != status.shutter_applied;
    if (controls.pair_hold_remaining_ms > 0U) {
        const uint32_t remaining_deciseconds =
            (controls.pair_hold_remaining_ms + 99U) / 100U;
        (void)snprintf(screen[3], sizeof(screen[3]), "PAIR IN %c.%cs",
                       (char)('0' + remaining_deciseconds / 10U),
                       (char)('0' + remaining_deciseconds % 10U));
    } else if (controls.pairing_active && !status.scanning) {
        (void)snprintf(screen[3], sizeof(screen[3]), "PREPARING PAIR");
    } else if (status.last_ble_error != 0) {
        (void)snprintf(screen[3], sizeof(screen[3]), "BLE ERR %-4d",
                       status.last_ble_error);
    } else if (sending) {
        (void)snprintf(screen[3], sizeof(screen[3]), "SENDING...");
    } else if (status.scanning) {
        (void)snprintf(screen[3], sizeof(screen[3]), "PAIR CAMERA");
    } else if (status.ready) {
        (void)snprintf(screen[3], sizeof(screen[3]), "READY");
    } else if (status.paired) {
        (void)snprintf(screen[3], sizeof(screen[3]), "PRESS BUTTON");
    } else {
        (void)snprintf(screen[3], sizeof(screen[3]), "HOLD PAIR TO SETUP");
    }
}

static void display_thread_entry(void *first, void *second, void *third)
{
    (void)first;
    (void)second;
    (void)third;

    for (;;) {
        char screen[SCREEN_ROWS][SCREEN_COLUMNS + 1] = {{0}};
        build_status_screen(screen);
        const int result = draw_screen(screen, false);
        if (result != 0) {
            LOG_WRN("Could not refresh OLED: %d", result);
        }
        k_msleep(DISPLAY_REFRESH_MS);
    }
}

int hardware_display_initialize(void)
{
    if (!device_is_ready(display)) {
        return -ENODEV;
    }

    int result = display_set_pixel_format(display, PIXEL_FORMAT_MONO10);
    if (result != 0) {
        result = display_set_pixel_format(display, PIXEL_FORMAT_MONO01);
    }
    if (result != 0) {
        return result;
    }
    prepare_status_font();
    result = cfb_framebuffer_init(display);
    if (result != 0) {
        return result;
    }
    result = select_status_font();
    if (result != 0) {
        return result;
    }
    if (cfb_get_display_parameter(display, CFB_DISPLAY_HEIGHT) <
            (int)(DISPLAY_VERTICAL_PADDING * 2U +
                  (SCREEN_ROWS - 1) * DISPLAY_LINE_HEIGHT +
                  STATUS_FONT_HEIGHT) ||
        cfb_get_display_parameter(display, CFB_DISPLAY_WIDTH) <
            (int)(DISPLAY_HORIZONTAL_PADDING * 2U +
                  SCREEN_COLUMNS * STATUS_FONT_WIDTH)) {
        return -ENOTSUP;
    }
    cfb_set_kerning(display, 0);
    result = cfb_framebuffer_clear(display, true);
    if (result != 0) {
        return result;
    }
    result = display_blanking_off(display);
    if (result != 0) {
        return result;
    }

    char startup[SCREEN_ROWS][SCREEN_COLUMNS + 1] = {{0}};
    (void)snprintf(startup[0], sizeof(startup[0]), "%-20.20s",
                   CONFIG_BT_DEVICE_NAME);
    (void)snprintf(startup[1], sizeof(startup[1]), "Starting...");
    result = draw_screen(startup, true);
    if (result != 0) {
        return result;
    }

    initialized = true;
    LOG_INF("OLED status display ready");
    return 0;
}

int hardware_display_start(void)
{
    if (!initialized) {
        return -EACCES;
    }
    if (started) {
        return 0;
    }

    k_tid_t thread_id = k_thread_create(
        &display_thread, display_thread_stack,
        K_THREAD_STACK_SIZEOF(display_thread_stack), display_thread_entry, NULL,
        NULL, NULL, DISPLAY_THREAD_PRIORITY, 0, K_NO_WAIT);
    (void)k_thread_name_set(thread_id, "status_oled");
    started = true;
    return 0;
}

bool hardware_display_available(void) { return true; }

#else

int hardware_display_initialize(void) { return 0; }

int hardware_display_start(void) { return 0; }

bool hardware_display_available(void) { return false; }

#endif
