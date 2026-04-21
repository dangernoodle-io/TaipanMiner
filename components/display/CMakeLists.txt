idf_component_register(
    SRCS "src/display.c" "src/font8x16.c" "src/logo_bits.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_lcd esp_driver_gpio esp_driver_spi esp_driver_i2c asic bb_nv
)
