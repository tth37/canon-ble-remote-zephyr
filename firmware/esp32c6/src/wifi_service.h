#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi_types_generic.h"

typedef struct {
    bool connected;
    bool has_ip;
    wifi_ap_record_t access_point;
    esp_netif_ip_info_t ip;
    uint8_t disconnect_reason;
} wifi_service_status_t;

esp_err_t wifi_service_initialize(void);
esp_err_t wifi_service_scan(wifi_ap_record_t *records, uint16_t *record_count);
esp_err_t wifi_service_join(const char *ssid, const char *password,
                            uint32_t timeout_ms);
esp_err_t wifi_service_leave(void);
esp_err_t wifi_service_get_status(wifi_service_status_t *status);
