#ifndef CANON_BLE_SERVICE_H
#define CANON_BLE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool initialized;
    bool host_synced;
    bool paired;
    bool connected;
    bool encrypted;
    bool ready;
    bool scanning;
    char camera_address[18];
    int last_ble_error;
} canon_ble_status_t;

esp_err_t canon_ble_service_initialize(void);
esp_err_t canon_ble_service_pair(uint32_t scan_seconds);
esp_err_t canon_ble_service_connect(void);
esp_err_t canon_ble_service_disconnect(void);
esp_err_t canon_ble_service_forget(void);
esp_err_t canon_ble_service_shutter(void);
esp_err_t canon_ble_service_focus(void);
void canon_ble_service_get_status(canon_ble_status_t *status);

#endif
