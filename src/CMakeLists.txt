idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES mining stratum wifi_prov nvs_flash esp_event asic
)
