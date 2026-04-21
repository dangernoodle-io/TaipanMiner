idf_component_register(
    SRCS "src/routes.c" "src/prov_form_html_gz.c" "src/theme_css_gz.c" "src/logo_svg_gz.c" "src/favicon_svg_gz.c" "src/mining_html_gz.c" "src/mining_js_gz.c" "src/prov_save_html_gz.c"
    INCLUDE_DIRS "include"
    REQUIRES mining stratum bb_wifi bb_prov bb_mdns bb_http bb_nv taipan_config bb_ota_pull bb_ota_push ota_validator bb_log board esp_http_server esp_app_format esp_timer esp_wifi esp_system nvs_flash json spi_flash
)
