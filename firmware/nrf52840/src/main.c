#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "canon_ble_zephyr.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    const canon_zephyr_result_t result = canon_ble_zephyr_initialize();
    if (result != CANON_ZEPHYR_OK) {
        LOG_ERR("Canon BLE initialization failed: %s",
                canon_ble_zephyr_result_name(result));
    } else {
        LOG_INF("Pro Micro nRF52840 serial services ready");
    }

    return 0;
}
