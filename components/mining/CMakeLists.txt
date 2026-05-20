idf_component_register(
    SRCS "src/mining.c" "src/mining_avg.c" "src/mining_pause.c" "src/mining_pause_espidf.c" "src/mining_pause_state.c" "src/mining_pool_stats.c" "src/sha256.c" "src/sha256_hw_ahb.c" "src/sha256_hw_dport.c" "src/work.c" "src/share_validate.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_timer soc esp_hw_support esp_driver_tsens stratum board bb_nv bb_system bb_log bb_event
    PRIV_REQUIRES esp_security
)
