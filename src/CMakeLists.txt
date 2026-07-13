# Handwired composition root (bb's autowire/bb_init walker is dead; see
# breadboard CLAUDE.md). Direct deps only: every component below is a
# header main.c includes directly (bb_log, bb_system, mining). No
# NVS/WiFi/HTTP/mDNS/ASIC deps yet (NVS/WiFi blocked on breadboard
# B1-840 — see src/main.c).
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bb_log bb_system mining
)
