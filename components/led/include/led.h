#pragma once

#ifdef ESP_PLATFORM

#include "esp_err.h"
#include <stdint.h>

esp_err_t led_init(void);
esp_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b);
esp_err_t led_off(void);

#endif
