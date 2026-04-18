idf_component_register(
    SRCS "src/asic_task.c" "src/crc.c" "src/pll.c" "src/asic_proto.c"
         "src/bm1370_ops.c" "src/bm1368_ops.c" "src/tps546.c" "src/emc2101.c"
    INCLUDE_DIRS "include"
    REQUIRES mining freertos driver esp_timer esp_driver_i2c esp_driver_gpio board stratum
)
