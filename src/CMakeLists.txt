idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES mining stratum display wifi_prov http_server led nv_config
)
