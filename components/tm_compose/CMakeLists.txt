# tm_compose -- TA-562: composes the mining + stratum tasks and
# the bb_bqueue work/result queues both sides bind against. See
# include/tm_compose.h.
#
# REQUIRES:
#   - bb_core -- bb_err_t appears in the public header (the only thing the
#     public header exposes)
#
# PRIV_REQUIRES (everything else is used only from src/, never the public
# header):
#   - bb_bqueue      -- bb_bqueue_t/bb_bqueue_create/etc, the two work/result
#     queues
#   - bb_task        -- bb_task_create/bb_task_config_t, spawns the mining +
#     stratum service tasks (ESP_PLATFORM only)
#   - bb_log         -- bb_log_i/w/e
#     (bb_clock_now_ms(), the stratum service loop's now_ms source, lives in
#     bb_core/include/bb_clock.h -- already covered by bb_core above)
#   - tm_mining      -- mining_work_t/mining_result_t/mining_queue_ops_t/
#     mining_set_queue_ops/g_miner_config
#   - tm_stratum     -- tm_stratum_v1_pool_client_ops/stratum_pool_client_ctx_t
#   - tm_pool_client -- tm_pool_client_ops_t/tm_pool_work_ops_t
#   - tm_pool_config -- tm_pool_cfg_t/tm_pool_config_get/_is_configured
#   - tm_pool_policy -- tm_pool_policy_active_idx
idf_component_register(
    SRCS "src/tm_compose_mining_stratum.c"
    INCLUDE_DIRS "include"
    REQUIRES bb_core
    PRIV_REQUIRES bb_bqueue bb_task bb_log tm_mining tm_stratum tm_pool_client tm_pool_config tm_pool_policy
)
