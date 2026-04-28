idf_component_register(
    SRCS "src/mining.c" "src/mining_pause.c" "src/mining_pause_espidf.c" "src/mining_pause_state.c" "src/sha256.c" "src/sha256_hw.c" "src/work.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_timer soc esp_hw_support esp_driver_tsens stratum board bb_nv
    PRIV_REQUIRES esp_security
)
