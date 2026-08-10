#include "wifi_service.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_HAS_IP_BIT BIT1

static const char *TAG = "wifi_service";
static EventGroupHandle_t wifi_events;
static esp_netif_t *station_netif;
static volatile uint8_t last_disconnect_reason;

static void handle_wifi_event(void *context, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    (void)context;
    (void)event_base;
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        last_disconnect_reason = event->reason;
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT | WIFI_HAS_IP_BIT);
    }
}

static void handle_ip_event(void *context, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    (void)context;
    (void)event_base;
    (void)event_data;
    if (event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_HAS_IP_BIT);
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        xEventGroupClearBits(wifi_events, WIFI_HAS_IP_BIT);
    }
}

esp_err_t wifi_service_initialize(void)
{
    wifi_events = xEventGroupCreate();
    if (wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Could not initialize TCP/IP");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG,
                        "Could not create event loop");
    station_netif = esp_netif_create_default_wifi_sta();
    if (station_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const wifi_init_config_t configuration = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&configuration), TAG,
                        "Could not initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   handle_wifi_event, NULL),
        TAG, "Could not register Wi-Fi events");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, handle_ip_event,
                                   NULL),
        TAG, "Could not register IP events");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG,
                        "Could not select RAM credential storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                        "Could not select station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Could not start Wi-Fi");
    esp_log_level_set("wifi", ESP_LOG_WARN);
    return ESP_OK;
}

esp_err_t wifi_service_scan(wifi_ap_record_t *records, uint16_t *record_count)
{
    if (records == NULL || record_count == NULL || *record_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const wifi_scan_config_t scan_configuration = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_configuration, true), TAG,
                        "Wi-Fi scan failed");
    return esp_wifi_scan_get_ap_records(record_count, records);
}

esp_err_t wifi_service_join(const char *ssid, const char *password,
                            uint32_t timeout_ms)
{
    if (ssid == NULL || password == NULL || ssid[0] == '\0' ||
        strlen(ssid) > 32U || strlen(password) > 64U || timeout_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t configuration = {0};
    memcpy(configuration.sta.ssid, ssid, strlen(ssid));
    memcpy(configuration.sta.password, password, strlen(password));
    configuration.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    configuration.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    configuration.sta.threshold.authmode = WIFI_AUTH_OPEN;
    configuration.sta.pmf_cfg.capable = true;
    configuration.sta.pmf_cfg.required = false;

    const esp_err_t disconnect_result = esp_wifi_disconnect();
    if (disconnect_result != ESP_OK &&
        disconnect_result != ESP_ERR_WIFI_NOT_CONNECT) {
        return disconnect_result;
    }
    xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT | WIFI_HAS_IP_BIT);
    last_disconnect_reason = 0;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &configuration), TAG,
                        "Could not apply station configuration");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Could not start connection");

    const EventBits_t bits = xEventGroupWaitBits(
        wifi_events, WIFI_HAS_IP_BIT, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(timeout_ms));
    if ((bits & WIFI_HAS_IP_BIT) == 0) {
        esp_wifi_disconnect();
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t wifi_service_leave(void)
{
    const esp_err_t result = esp_wifi_disconnect();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_CONNECT) {
        return result;
    }
    wifi_config_t empty_configuration = {0};
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_config(WIFI_IF_STA, &empty_configuration), TAG,
        "Could not clear station credentials");
    xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT | WIFI_HAS_IP_BIT);
    last_disconnect_reason = 0;
    return ESP_OK;
}

esp_err_t wifi_service_get_status(wifi_service_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(status, 0, sizeof(*status));

    const EventBits_t bits = xEventGroupGetBits(wifi_events);
    status->connected = (bits & WIFI_CONNECTED_BIT) != 0;
    status->has_ip = (bits & WIFI_HAS_IP_BIT) != 0;
    status->disconnect_reason = last_disconnect_reason;

    if (status->connected) {
        ESP_RETURN_ON_ERROR(esp_wifi_sta_get_ap_info(&status->access_point), TAG,
                            "Could not get access point information");
    }
    if (status->has_ip) {
        ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(station_netif, &status->ip), TAG,
                            "Could not get IP information");
    }
    return ESP_OK;
}
