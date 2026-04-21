idf_component_register(
    SRCS "src/ota_validator.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update nv_config esp_app_format esp_timer
)
