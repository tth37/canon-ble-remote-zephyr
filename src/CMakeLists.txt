idf_component_register(SRCS "main.c" "display.c" "input.c" "snake_game.c" "flappy_game.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_adc esp_driver_gpio esp_driver_i2c)
