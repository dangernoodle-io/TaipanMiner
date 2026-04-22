#pragma once

#ifdef ESP_PLATFORM

#include "bb_nv.h"
#include <stdint.h>

bb_err_t led_init(void);
bb_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b);
bb_err_t led_off(void);

#endif
