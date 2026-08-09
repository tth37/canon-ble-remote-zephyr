idf_component_register(SRCS "main.c" "display.c" "joystick.c" "renderer3d.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_adc esp_driver_gpio esp_driver_i2c)
