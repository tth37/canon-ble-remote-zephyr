idf_component_register(
    SRCS "main.c"
         "console_app.c"
         "canon_ble_commands.c"
         "canon_ble_service.c"
         "system_commands.c"
         "wifi_commands.c"
         "wifi_service.c"
    INCLUDE_DIRS "."
    REQUIRES bt console esp_event esp_netif esp_wifi heap nvs_flash spi_flash
)
