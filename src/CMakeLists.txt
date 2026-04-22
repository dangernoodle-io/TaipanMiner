idf_component_register(
    SRCS "main.c" "partition_fixup.c" "partition_fixup_data.c"
    INCLUDE_DIRS "."
    REQUIRES mining stratum bb_wifi bb_prov bb_mdns bb_http bb_nv taipan_config asic board spi_flash display led bb_log bb_ntp bb_system bb_ota_pull bb_ota_push
)
