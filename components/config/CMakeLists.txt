idf_component_register(
    SRCS "src/config.c"
    INCLUDE_DIRS "include"
    REQUIRES bb_nv bb_log bb_mdns bb_manifest
)
