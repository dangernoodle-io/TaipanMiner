idf_component_register(
    SRCS "src/stratum.c"
    INCLUDE_DIRS "include"
    REQUIRES mining json esp_netif nv_config board
)
