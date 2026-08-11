#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "canon/remote.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    const canon_remote_result_t result = canon_remote_initialize();
    if (result != CANON_REMOTE_OK) {
        LOG_ERR("Canon BLE initialization failed: %s",
                canon_remote_result_name(result));
    } else {
        LOG_INF("Canon remote ready on %s", CONFIG_BOARD_TARGET);
    }

    return 0;
}
