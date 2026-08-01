# tm_pool_client -- header-only pool-client seam (tm_pool_client_ops_t) +
# the cross-core work/result seam it owns (tm_pool_work_seam.h,
# tm_pool_work_ops_t -- relocated here from tm_stratum, TA-571 firmware
# review finding #1: it is protocol-agnostic, built entirely from tm_mining's
# own types, with zero stratum-wire content, so it belongs to the
# abstraction that owns the cross-core seam, not to any one protocol
# backend). See include/tm_pool_client.h + include/tm_pool_work_seam.h.
# No src/: this component is purely an interface other components implement
# (tm_stratum's V1 adapter, stratum_pool_client.c) and the composition root
# drives against.
#
# REQUIRES:
#   - bb_core        -- bb_err_t appears in the public header
#   - tm_pool_config -- tm_pool_cfg_t (tm_pool_cfg.h) appears in the public header
#   - tm_mining      -- mining_work_t (work.h) / mining_result_t (mining.h)
#                        appear in tm_pool_work_seam.h
#
# The dependency with tm_stratum is now ONE-DIRECTIONAL: tm_stratum REQUIRES
# tm_pool_client (for tm_pool_client_ops_t + tm_pool_work_ops_t); this
# component has no REQUIRES on tm_stratum at all.
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES bb_core tm_pool_config tm_mining
)
