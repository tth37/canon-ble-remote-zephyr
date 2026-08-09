idf_component_register(SRCS "main.c" "display.c" "joystick.c" "arduboy2_port.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_adc esp_driver_gpio esp_driver_i2c esp_timer)
