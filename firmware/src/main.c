#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "canon/remote.h"
#include "hardware/controls.h"
#include "hardware/display.h"
#include "hardware/indicator.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    int result = hardware_display_initialize();
    const bool display_ready = result == 0 && hardware_display_available();
    if (result != 0) {
        LOG_WRN("Status display unavailable: %d", result);
    }

    result = hardware_controls_initialize();
    if (result != 0) {
        LOG_ERR("Camera button initialization failed: %d", result);
    }

    result = hardware_indicator_initialize();
    const bool indicator_ready = result == 0 && hardware_indicator_available();
    if (result != 0) {
        LOG_WRN("Status LED unavailable: %d", result);
    }

    const canon_remote_result_t remote_result = canon_remote_initialize();
    if (remote_result != CANON_REMOTE_OK) {
        LOG_ERR("Canon BLE initialization failed: %s",
                canon_remote_result_name(remote_result));
    } else {
        LOG_INF("Canon remote ready on %s", CONFIG_BOARD_TARGET);
    }

    if (display_ready) {
        result = hardware_display_start();
        if (result != 0) {
            LOG_WRN("Could not start status display: %d", result);
        }
    }

    if (remote_result == CANON_REMOTE_OK && hardware_controls_available()) {
        result = hardware_controls_start();
        if (result != 0) {
            LOG_ERR("Could not start camera buttons: %d", result);
        }
    }

    if (indicator_ready) {
        result = hardware_indicator_start();
        if (result != 0) {
            LOG_WRN("Could not start status LED: %d", result);
        }
    }

    return 0;
}
