idf_component_register(
    SRCS "src/http_server.c" "src/prov_form_html.c" "src/theme_css.c" "src/logo_svg.c"
    INCLUDE_DIRS "include"
    REQUIRES board mining nv_config wifi_prov esp_http_server app_update json esp_app_format esp_timer
)
