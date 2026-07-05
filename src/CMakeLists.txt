# Phase 1 skeleton — bb_init host shell only.
# Direct deps only: every component below is either called from main.c
# (bb_log, bb_init, bb_system) or must be linked so bb_init's registry
# tiers activate it (bb_nv, bb_wifi, bb_http, bb_mdns, bb_ntp, bb_info,
# bb_board, bb_manifest, bb_openapi — all Kconfig-default autoregister=y).
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bb_core bb_log bb_nv bb_wifi bb_http bb_mdns bb_init bb_system
             bb_ntp bb_info bb_board bb_manifest bb_openapi
)
