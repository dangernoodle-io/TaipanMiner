idf_component_register(
    SRCS "src/wifi_prov.c" "src/wifi_reconn.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event esp_timer esp_system nv_config lwip mdns board esp_app_format stratum
)
