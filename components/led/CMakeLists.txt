idf_component_register(
    SRCS "src/led.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_gpio bb_nv
)
