idf_component_register(
    SRCS "src/log_stream.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_ringbuf freertos log
)
