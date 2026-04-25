idf_component_register(
    SRCS "src/stratum.c" "src/stratum_utils.c" "src/stratum_backoff.c" "src/stratum_watchdogs.c"
    INCLUDE_DIRS "include"
    REQUIRES mining bb_json esp_netif bb_nv taipan_config board bb_ota_pull ota_validator bb_wifi
)
