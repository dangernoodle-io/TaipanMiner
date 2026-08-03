idf_component_register(
    SRCS "src/mining.c" "src/tm_mining_producer.c" "src/mining_avg.c" "src/mining_pool_stats.c" "src/sha256.c" "src/sha256_hw_dport.c" "src/work.c" "src/share_validate.c"
    INCLUDE_DIRS "include"
    # Public: mining.h includes bb_serialize.h + (guarded) bb_lock.h/bb_lock_once.h;
    # sha256.h includes bb_core.h + (guarded) esp_attr.h; sha256_hw_dport_kernel.h
    # includes (guarded) soc/dport_access.h + soc/hwcrypto_reg.h, which pulls in
    # esp_hw_support publicly too. freertos is NOT public: mining.h does not
    # include freertos/*.h -- only mining.c (a private .c file) does.
    REQUIRES bb_serialize bb_core esp_common soc esp_hw_support
    # Private: only referenced from .c files, never a public header.
    PRIV_REQUIRES bb_log bb_timer bb_wdt bb_system freertos
)
