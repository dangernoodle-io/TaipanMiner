if(CONFIG_KNOT_ENABLED)
    set(KNOT_SRCS "src/knot.c" "src/knot_table.c")
else()
    set(KNOT_SRCS)
endif()

# REQUIRES is declared unconditionally so the dependency graph is stable
# regardless of CONFIG_KNOT_ENABLED. The sources (and thus the actual
# symbols) are gated above; when off, an empty archive is produced.
idf_component_register(
    SRCS ${KNOT_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES bb_log bb_mdns bb_nv bb_timer freertos
)
