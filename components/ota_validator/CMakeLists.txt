idf_component_register(
    SRCS "src/ota_validator.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update bb_nv esp_app_format esp_timer
)
