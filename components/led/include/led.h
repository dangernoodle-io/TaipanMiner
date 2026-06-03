#pragma once

#ifdef ESP_PLATFORM

#include "bb_nv.h"
#include <stdint.h>
#include <stdbool.h>

bb_err_t led_init(void);
bb_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b);
bb_err_t led_off(void);
// Mining heartbeat: slow dim breathe to show the board is alive + hashing.
// Overridden by led_set_color() (OTA status); no-op on boards with no LED.
bb_err_t led_set_mining(bool on);

#endif
