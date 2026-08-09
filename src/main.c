#include "display.h"
#include "display_config.h"
#include "esp_err.h"
#include "joystick.h"
#include "rift_runner.h"

static void show_calibration(void)
{
    display_clear();
    display_draw_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 12,
                                         "LEFT STICK", 1);
    display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH, DISPLAY_PANEL_WIDTH,
                                         12, "RIGHT STICK", 1);
    display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 29,
                                         "RELEASE", 1);
    display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH,
                                         DISPLAY_PANEL_WIDTH, 29, "BOTH", 1);
    display_draw_centered_text_in_region(0, DISPLAY_PANEL_WIDTH, 43,
                                         "CALIBRATE", 1);
    display_draw_centered_text_in_region(DISPLAY_PANEL_WIDTH,
                                         DISPLAY_PANEL_WIDTH, 43, "STICKS", 1);
    ESP_ERROR_CHECK(display_present());
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_initialize());
    ESP_ERROR_CHECK(joysticks_initialize());
    show_calibration();
    joysticks_calibrate();
    rift_runner_run();
}
