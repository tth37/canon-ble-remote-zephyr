#pragma once

#include "esp_err.h"

#define BLE_DISPLAY_DEVICE_NAME "Codex Display"
#define BLE_DISPLAY_SERVICE_UUID "7b1e0001-6d8f-4b7a-9c2d-4a7f1d2e3c40"
#define BLE_DISPLAY_TEXT_UUID "7b1e0002-6d8f-4b7a-9c2d-4a7f1d2e3c40"
#define BLE_DISPLAY_MAX_TEXT_LENGTH 120

typedef enum {
    BLE_DISPLAY_EVENT_ADVERTISING,
    BLE_DISPLAY_EVENT_CONNECTED,
    BLE_DISPLAY_EVENT_DISCONNECTED,
    BLE_DISPLAY_EVENT_TEXT_RECEIVED,
} ble_display_event_type_t;

typedef struct {
    ble_display_event_type_t type;
    char text[BLE_DISPLAY_MAX_TEXT_LENGTH + 1];
} ble_display_event_t;

typedef void (*ble_display_event_callback_t)(const ble_display_event_t *event, void *context);

esp_err_t ble_display_service_initialize(ble_display_event_callback_t callback, void *context);
