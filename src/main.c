#include "console_app.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_service.h"

static const char *TAG = "main";

static void initialize_nvs(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
}

void app_main(void)
{
    initialize_nvs();
    ESP_ERROR_CHECK(wifi_service_initialize());
    ESP_ERROR_CHECK(console_app_start());
    ESP_LOGI(TAG, "Serial Wi-Fi shell is ready");
}
