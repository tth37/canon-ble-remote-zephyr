idf_component_register(
    SRCS "main.c"
         "console_app.c"
         "system_commands.c"
         "wifi_commands.c"
         "wifi_service.c"
    INCLUDE_DIRS "."
    REQUIRES console esp_event esp_netif esp_wifi heap nvs_flash spi_flash
)
