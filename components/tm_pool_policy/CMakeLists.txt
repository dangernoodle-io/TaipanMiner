# tm_pool_policy -- the pool selection engine (MANUAL/FAILOVER/AUTO_ROTATE)
# over bb_config/bb_storage, internally synchronized via bb_core's bb_lock,
# driving a caller-supplied tm_pool_client_ops_t. See include/tm_pool_policy.h.
#
# REQUIRES:
#   - bb_core        -- bb_err_t appears in the public header
#   - tm_pool_client -- tm_pool_client_ops_t appears in the public header
#     (tm_pool_policy_tick()'s driven vtable parameter)
#
# PRIV_REQUIRES:
#   - bb_config    -- bb_config_field_t/bb_config_get_*/set_*, used only from
#     src/tm_pool_policy.c, never a public header
#   - tm_pool_config -- TM_POOL_MAX and tm_pool_config_is_configured(), used
#     only from src/tm_pool_policy.c (TM_POOL_MAX itself is never re-exported
#     from tm_pool_policy.h -- callers already get it via tm_pool_client.h's
#     own transitive tm_pool_cfg.h include)
idf_component_register(
    SRCS "src/tm_pool_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES bb_core tm_pool_client
    PRIV_REQUIRES bb_config tm_pool_config
)
