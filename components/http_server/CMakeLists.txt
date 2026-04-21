idf_component_register(
    SRCS "src/http_server.c" "src/http_utils.c" "src/prov_form_html_gz.c" "src/theme_css_gz.c" "src/logo_svg_gz.c" "src/favicon_svg_gz.c" "src/mining_html_gz.c" "src/mining_js_gz.c" "src/prov_save_html_gz.c"
    INCLUDE_DIRS "include"
    REQUIRES board mining nv_config taipan_config wifi_prov esp_http_server app_update json esp_app_format esp_timer ota_pull spi_flash log_stream nvs_flash
)
