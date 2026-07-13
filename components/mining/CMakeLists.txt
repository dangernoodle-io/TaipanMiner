idf_component_register(
    SRCS "src/mining.c" "src/mining_wire.c" "src/mining_avg.c" "src/mining_pool_stats.c" "src/sha256.c" "src/sha256_hw_dport.c" "src/work.c" "src/share_validate.c"
    INCLUDE_DIRS "include"
    # Public: mining.h includes bb_serialize.h + (guarded) freertos/*.h;
    # sha256.h includes bb_core.h + (guarded) esp_attr.h; sha256_hw_dport_kernel.h
    # includes (guarded) soc/dport_access.h + soc/hwcrypto_reg.h, which pulls in
    # esp_hw_support publicly too.
    REQUIRES bb_serialize bb_core freertos esp_common soc esp_hw_support
    # Private: only referenced from .c files, never a public header.
    PRIV_REQUIRES bb_log bb_timer bb_wdt bb_system
)
