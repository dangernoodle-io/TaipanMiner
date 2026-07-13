# PR 1 skeleton — handwired composition root (bb's autowire/bb_init walker
# is dead; see breadboard CLAUDE.md). Direct deps only: every component
# below is a header main.c includes directly (bb_log, bb_system). No
# NVS/WiFi/HTTP/mDNS/ASIC deps yet (NVS/WiFi blocked on breadboard
# B1-840 — see src/main.c).
#
# stratum (TA-560): main.c does not call into it yet (no real pool config
# to connect with -- see the MINING/STRATUM SLOT comment in main.c), but it
# is REQUIRES'd here so the component builds and links as part of the real
# firmware image, exercising stratum_transport_esp.c's ESP_PLATFORM path
# for real on-device link verification ahead of the task-wiring PR.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bb_log bb_system stratum
)
