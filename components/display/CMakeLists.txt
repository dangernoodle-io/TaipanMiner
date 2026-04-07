idf_component_register(
    SRCS "src/display.c" "src/font8x16.c" "src/logo_rgb565.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_lcd esp_driver_gpio esp_driver_spi esp_driver_i2c asic
)
