idf_component_register(SRCS "main.c" "display.c" "ble_display_service.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt esp_driver_i2c nvs_flash)
