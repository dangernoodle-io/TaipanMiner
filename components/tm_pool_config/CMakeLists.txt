# tm_pool_config -- 3-slot pool configuration over bb_config/bb_storage.
# See include/tm_pool_cfg.h.
#
# REQUIRES:
#   - bb_core -- bb_err_t appears in the public header (tm_pool_cfg.h)
#
# PRIV_REQUIRES:
#   - bb_config -- bb_config_field_t/bb_config_get_*/set_*/erase, used only
#     from src/tm_pool_config.c, never a public header
idf_component_register(
    SRCS "src/tm_pool_config.c"
    INCLUDE_DIRS "include"
    REQUIRES bb_core
    PRIV_REQUIRES bb_config
)
