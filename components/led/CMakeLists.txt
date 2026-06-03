idf_component_register(
    SRCS "src/led.c"
    INCLUDE_DIRS "include"
    REQUIRES board bb_led bb_led_apa102 bb_led_pwm bb_led_anim bb_log bb_nv
)
