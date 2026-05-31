include("${CMAKE_CURRENT_LIST_DIR}/../../.breadboard/cmake/bb_embed.cmake")

bb_embed_assets(
    OUT_SRCS _embed_srcs
    ASSETS
        ../../webui/miner/dist/index.html:index_html_gz
        ../../webui/miner/dist/assets/index.js:index_js_gz
        ../../webui/miner/dist/assets/index.css:index_css_gz
        ../../webui/miner/dist/assets/runtime.js:runtime_js_gz
        ../../webui/miner/dist/assets/vendor.js:vendor_js_gz
        ../../webui/miner/dist/assets/index2.js:index2_js_gz
        ../../webui/miner/dist/assets/index3.js:index3_js_gz
        ../../webui/miner/dist/assets/Pool.js:pool_js_gz
        ../../webui/miner/dist/assets/Update.js:update_js_gz
        ../../webui/miner/dist/assets/Diagnostics.js:diagnostics_js_gz
        ../../webui/miner/dist/assets/Settings.js:settings_js_gz
        ../../webui/miner/dist/assets/History.js:history_js_gz
        ../../webui/miner/dist/assets/Knot.js:knot_js_gz
        ../../webui/miner/dist/logo.svg:logo_svg_gz
        ../../webui/miner/dist/favicon.svg:favicon_svg_gz
        ../../webui/prov/dist/index.html:prov_index_html_gz
        ../../webui/prov/dist/assets/index.js:prov_index_js_gz
        ../../webui/prov/dist/assets/index.css:prov_index_css_gz
)

idf_component_register(
    SRCS "src/routes.c" "src/routes_json.c" ${_embed_srcs}
    INCLUDE_DIRS "include"
    REQUIRES mining stratum bb_wifi bb_prov bb_mdns bb_http bb_nv bb_diag config bb_ota_pull bb_ota_push bb_ota_validator bb_log board bb_info bb_board bb_timer bb_json spi_flash asic knot bb_system
)
