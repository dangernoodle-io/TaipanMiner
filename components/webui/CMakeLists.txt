include("${CMAKE_CURRENT_LIST_DIR}/../../.breadboard/cmake/bb_embed.cmake")

bb_embed_site(OUT_SRCS _miner_srcs TABLE webui_miner_assets DIST_DIR ../../webui/miner/dist)
bb_embed_site(OUT_SRCS _prov_srcs  TABLE webui_prov_site   DIST_DIR ../../webui/prov/dist)

idf_component_register(
    SRCS "src/routes.c" "src/routes_json.c" ${_miner_srcs} ${_prov_srcs}
    INCLUDE_DIRS "include"
    REQUIRES mining stratum bb_wifi bb_prov bb_mdns bb_http bb_nv bb_diag config bb_ota_pull bb_ota_push bb_ota_validator bb_log board bb_info bb_board bb_timer bb_json spi_flash asic knot bb_system led bb_fan_routes bb_fan bb_power bb_health bb_sensors bb_section app_update esp_hw_support heap
)
