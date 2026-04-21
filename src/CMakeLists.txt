idf_component_register(
    SRCS "main.c" "partition_fixup.c" "partition_fixup_data.c"
    INCLUDE_DIRS "."
    REQUIRES mining stratum wifi_prov nv_config taipan_config nvs_flash esp_event asic board spi_flash display led log_stream ota_pull ota_push ota_validator
)
