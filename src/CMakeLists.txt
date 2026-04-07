idf_component_register(
    SRCS "main.c" "partition_fixup.c" "partition_fixup_data.c"
    INCLUDE_DIRS "."
    REQUIRES mining stratum wifi_prov nvs_flash esp_event asic board spi_flash
)
