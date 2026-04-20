idf_component_register(
    SRCS "src/http_server.c" "src/http_utils.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_app_format esp_timer taipan_web nvs_flash
)
