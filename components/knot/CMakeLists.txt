idf_component_register(
    SRCS "src/knot.c" "src/knot_table.c"
    INCLUDE_DIRS "include"
    REQUIRES bb_log bb_mdns bb_nv bb_timer freertos
)
