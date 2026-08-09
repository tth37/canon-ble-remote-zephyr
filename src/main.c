#include <stdbool.h>

#include "display.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "joystick.h"
#include "renderer3d.h"

#define INPUT_FRAME_MS 25

static void render_calibration_screen(void)
{
    display_clear();
    display_draw_rectangle(0, 0, 128, 64);
    display_draw_centered_text(15, "RELEASE STICK", 1);
    display_draw_centered_text(33, "CALIBRATING", 1);
    ESP_ERROR_CHECK(display_present());
}

static void render_cube(const camera3d_t *camera)
{
    display_clear();
    display_draw_text(1, 0, "3D CUBE", 1);
    display_draw_text(79, 0, "SW RESET", 1);
    display_fill_rectangle(0, 9, 128, 1);
    renderer3d_draw_cube(camera);
    ESP_ERROR_CHECK(display_present());
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_initialize());
    ESP_ERROR_CHECK(joystick_initialize());
    render_calibration_screen();
    joystick_calibrate();

    camera3d_t camera;
    renderer3d_reset_camera(&camera);
    render_cube(&camera);

    while (true) {
        joystick_state_t joystick;
        joystick_read(&joystick);
        const bool camera_moved = joystick.x != 0.0F || joystick.y != 0.0F;
        if (joystick.button_pressed) {
            renderer3d_reset_camera(&camera);
        } else if (camera_moved) {
            renderer3d_orbit_camera(&camera, joystick.x, joystick.y);
        }
        if (camera_moved || joystick.button_pressed) {
            render_cube(&camera);
        }
        vTaskDelay(pdMS_TO_TICKS(INPUT_FRAME_MS));
    }
}
