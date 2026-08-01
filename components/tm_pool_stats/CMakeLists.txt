# tm_pool_stats -- per-pool lifetime record + scoreboard + sanitizers over
# bb_config/bb_storage/bb_timer/bb_str, internally synchronized via
# bb_core's bb_lock. See include/tm_pool_stats.h.
#
# REQUIRES:
#   - bb_core        -- bb_err_t appears in the public header; also the
#     source of bb_lock/bb_lock_once (src/tm_pool_stats.c's internal lock)
#   - tm_pool_config -- TM_POOL_MAX appears in the public header (via its
#     tm_pool_cfg.h -- included for the constant only; tm_pool_config's own
#     tm_pool_cfg_t/tm_pool_config_* API is unused here)
#
# PRIV_REQUIRES:
#   - bb_config -- bb_config_field_t/bb_config_get_blob/set_blob/erase,
#     used only from src/tm_pool_stats.c, never a public header
#   - bb_timer  -- the periodic flush timer, ESP-IDF-only (gated in src)
#   - bb_log    -- sanitizer corruption logging
#   - bb_str    -- bb_strlcpy (job_id fill) + bb_str_bytes_to_hex (display
#     helper), used only from src/tm_pool_scoreboard.c, never a public
#     header
idf_component_register(
    SRCS "src/tm_pool_stats.c" "src/tm_pool_scoreboard.c"
    INCLUDE_DIRS "include"
    REQUIRES bb_core tm_pool_config
    PRIV_REQUIRES bb_config bb_timer bb_log bb_str
)
