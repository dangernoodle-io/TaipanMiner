# partition_fixup_data.c is generated per-env into ${CMAKE_BINARY_DIR} by
# scripts/embed_partitions.py (TA-332: prevents parallel-build races on a
# shared src/ output).

# Board-specific display backend: tdongle-s3 uses ST7735 (SPI), bitaxe-* use SSD1306 (I2C).
# Both backends share bb_display; only one is linked per build to avoid spurious
# probe/init attempts for the absent panel type.
if(FIRMWARE_BOARD MATCHES "^tdongle")
    set(_display_backend bb_display_st77xx)
elseif(FIRMWARE_BOARD MATCHES "^bitaxe")
    set(_display_backend bb_display_ssd1306)
else()
    # esp32-wroom32 and unknown boards: no display; include ssd1306 as a no-op
    # (probe will fail gracefully with -1 pins).
    set(_display_backend bb_display_ssd1306)
endif()

idf_component_register(
    SRCS "main.c" "partition_fixup.c" "partition_fixup_decision.c" "${CMAKE_BINARY_DIR}/partition_fixup_data.c"
          "ui/ui.c"
    INCLUDE_DIRS "." "ui"
    REQUIRES mining stratum bb_wifi bb_prov bb_mdns bb_http bb_nv config asic board spi_flash
             bb_display ${_display_backend}
             led bb_log bb_ntp bb_system bb_ota_pull bb_ota_push bb_openapi bb_manifest
)
