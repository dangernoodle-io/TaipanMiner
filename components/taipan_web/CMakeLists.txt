include("${CMAKE_CURRENT_LIST_DIR}/../../.breadboard/cmake/bb_embed.cmake")

bb_embed_assets(
    OUT_SRCS _embed_srcs
    ASSETS
        prov_form.html:prov_form_html_gz
        theme.css:theme_css_gz
        ../../webui/dist/index.html:index_html_gz
        ../../webui/dist/assets/index.js:index_js_gz
        ../../webui/dist/assets/index.css:index_css_gz
        ../../webui/dist/logo.svg:logo_svg_gz
        ../../webui/dist/favicon.svg:favicon_svg_gz
        prov_save.html:prov_save_html_gz
)

idf_component_register(
    SRCS "src/routes.c" "src/routes_json.c" ${_embed_srcs}
    INCLUDE_DIRS "include"
    REQUIRES mining stratum bb_wifi bb_prov bb_mdns bb_http bb_nv taipan_config bb_ota_pull bb_ota_push bb_ota_validator bb_log board bb_info bb_board esp_timer bb_json spi_flash asic knot
)
