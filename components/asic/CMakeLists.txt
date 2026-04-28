idf_component_register(
    SRCS "src/asic_task.c" "src/asic_metric_avg.c" "src/asic_pause_coalesce.c" "src/asic_drop_detect.c" "src/asic_drop_log.c" "src/asic_chip_routing.c" "src/asic_nonce_dedup.c" "src/asic_share_validator.c" "src/crc.c" "src/pll.c" "src/asic_proto.c"
         "src/bm1370_ops.c" "src/bm1368_ops.c" "src/tps546.c" "src/emc2101.c"
    INCLUDE_DIRS "include"
    REQUIRES mining freertos driver esp_timer esp_driver_i2c esp_driver_gpio board stratum bb_nv
)
