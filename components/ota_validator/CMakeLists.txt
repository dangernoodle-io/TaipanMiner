idf_component_register(
    SRCS "src/ota_validator.c"
    INCLUDE_DIRS "include"
    REQUIRES bb_ota_validator esp_timer bb_log
)
