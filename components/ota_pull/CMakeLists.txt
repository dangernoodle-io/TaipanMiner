idf_component_register(
    SRCS "src/ota_pull.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_https_ota esp_http_client app_update
             esp_app_format json board mining log esp-tls mbedtls
)
