#pragma once

#ifdef ESP_PLATFORM

#include "bb_nv.h"
#include <stdint.h>
#include <stdbool.h>

bb_err_t led_init(void);
bb_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b);
bb_err_t led_off(void);
// Mining heartbeat: fades the boot solid into a slow dim breathe. Overridden by
// led_set_color()/led_blink() (OTA status); no-op on boards with no LED.
bb_err_t led_set_mining(bool on);
// Flash the status LED at level_pct brightness (OTA "updating"). period_ms is the
// full on+off cycle. No-op on boards with no LED.
bb_err_t led_blink(uint8_t level_pct, uint32_t period_ms);
// Fire-and-forget celebratory flash on block found. Gold on RGB LEDs; a brief
// bright pulse on single-channel PWM. Leaves the animator in the PULSE pattern
// until the next led_set_mining()/led_blink() call restores the heartbeat.
bb_err_t led_flash_block_found(void);

#endif
