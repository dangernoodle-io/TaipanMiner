#include "led.h"

#ifdef ESP_PLATFORM

#include "board.h"
#include "bb_log.h"
#include "bb_led.h"
#include "bb_led_apa102.h"
#include "bb_led_pwm.h"
#include "bb_led_rgb_pwm.h"
#include "bb_led_anim.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led";
static bb_led_handle_t s_led;
static bb_led_anim_handle_t s_anim;

// Board-specific status-LED backend. BB_ERR_NOT_SUPPORTED → board has no LED.
static bb_err_t led_backend_open(bb_led_handle_t *out) {
#if defined(BOARD_TDONGLE_S3)
    bb_led_apa102_cfg_t cfg = {
        .pin_clk = PIN_LED_CLK,
        .pin_din = PIN_LED_DIN,
        .led_count = 1,
        .global_brightness_31 = 31,
    };
    return bb_led_apa102_open(&cfg, out);
#elif defined(BOARD_ESP32_S2_MINI) || defined(BOARD_ESP32_WROOM32)
    bb_led_pwm_cfg_t cfg = {
        .gpio = PIN_STATUS_LED,
        .freq_hz = 5000,
        // 13-bit duty: a dim gamma'd breathe at 8-bit only had ~1/255 steps near
        // the floor and looked like a slow blink; 13-bit renders it smoothly.
        .resolution_bits = 13,
        .active_low = false,
    };
    return bb_led_pwm_open(&cfg, out);
#elif defined(BOARD_ESP32_C3_SUPERMINI)
    bb_led_pwm_cfg_t cfg = {
        .gpio = PIN_STATUS_LED,
        .freq_hz = 5000,
        .resolution_bits = 13,
        .active_low = true,   // C3 SuperMini onboard LED (GPIO8) is active-low
    };
    return bb_led_pwm_open(&cfg, out);
#elif defined(BOARD_ESP32_WROOM32_CYD)
    // CYD common-anode (active-low) RGB LED — Elegoo 2432S028R '-32E':
    // R=GPIO22, G=GPIO16, B=GPIO17. Full 3-channel RGB via bb_led_rgb_pwm so the
    // status colors (boot/prov/mining/OTA) render instead of collapsing to red.
    bb_led_rgb_pwm_cfg_t cfg = {
        .gpio_r = PIN_LED_R,
        .gpio_g = PIN_LED_G,
        .gpio_b = PIN_LED_B,
        .freq_hz = 5000,
        .resolution_bits = 13,
        .active_low = true,
    };
    return bb_led_rgb_pwm_open(&cfg, out);
#else
    (void)out;
    return BB_ERR_UNSUPPORTED;  // bitaxe: no status LED
#endif
}

// Render a solid color. RGB LEDs (APA102) show the color; single-channel LEDs
// (PWM) show max(r,g,b) as brightness — so the shared bb_ota_progress_cb_t color
// values work on both without the caller knowing the backend.
static bb_err_t led_render(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led) return BB_OK;
    if (bb_led_caps(s_led) & BB_LED_CAP_RGB) {
        bb_led_set_color(s_led, 0, r, g, b);
    } else {
        uint8_t mx = r > g ? r : g;
        if (b > mx) mx = b;
        bb_led_set_brightness(s_led, 0, (uint8_t)((mx * 100) / 255));
    }
    return bb_led_flush(s_led);
}

bb_err_t led_init(void) {
    if (led_backend_open(&s_led) != BB_OK) {
        s_led = NULL;  // LED-less board: all ops become no-ops
        return BB_OK;
    }

    // Designate as the primary status LED so bb_led_info can report it.
    bb_led_set_primary(s_led);

    // Animator drives patterns (e.g. the mining heartbeat) off a bb_timer, so it
    // keeps ticking even while the single-core miner saturates the CPU.
    bb_led_anim_cfg_t acfg = { .led = s_led, .tick_period_ms = 0, .auto_start_timer = true };
    bb_led_anim_attach(&acfg, &s_anim);

    bb_log_i(TAG, "status LED ready (%s)",
             (bb_led_caps(s_led) & BB_LED_CAP_RGB) ? "rgb" : "pwm");

    // Boot indicator: a steady 50% until mining starts, then it fades into the
    // heartbeat (see led_set_mining). On the S2's single-channel PWM the 50% is
    // duty; on RGB LEDs it's white at 50%. The animator timer keeps it lit.
    bb_led_anim_pattern_t boot = {
        .kind  = BB_ANIM_SOLID,
        .solid = { .r = 255, .g = 255, .b = 255, .brightness_pct = 50 },
    };
    // APA102 white reads bright at 50%; keep the boot indicator dim on the RGB LED.
    // The S2's single-channel PWM keeps 50% duty (its boot indicator reads fine).
    if (bb_led_caps(s_led) & BB_LED_CAP_RGB) {
        boot.solid.brightness_pct = 3;
    }
    bb_led_anim_set(s_anim, &boot);
    return BB_OK;
}

// OTA / status color (shared progress callback). Overrides any running animation
// (e.g. the mining heartbeat) until led_set_mining()/led_off() is called again.
bb_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    if (s_anim) bb_led_anim_pause(s_anim);
    return led_render(r, g, b);
}

bb_err_t led_off(void) {
    if (s_anim) bb_led_anim_pause(s_anim);
    return led_render(0, 0, 0);
}

bb_err_t led_set_mining(bool on) {
    if (!s_anim) return BB_OK;
    if (!on) {
        bb_led_anim_pause(s_anim);
        return led_render(0, 0, 0);
    }
    // Resume in case a prior OTA status (led_set_color / led_blink) paused it.
    bb_led_anim_resume(s_anim);
    // Green base on color LEDs; breathe modulates brightness. On a single-channel
    // PWM LED there's no color — the breathe drives the duty.
    if (bb_led_caps(s_led) & BB_LED_CAP_RGB) {
        bb_led_set_color(s_led, 0, 0, 255, 0);
    }
    // Fade the boot solid 50% into the mining-heartbeat breathe (~0.8s handoff).
    bb_led_anim_pattern_t pat = {
        .kind = BB_ANIM_BREATHE,
        .breathe = { .period_ms = 3000, .min_pct = 1, .max_pct = 10 },
    };
    if (bb_led_caps(s_led) & BB_LED_CAP_RGB) {
        // Tune the RGB heartbeat for the APA102: a dim, slow green breath. The
        // driver's combined 5-bit-global + 8-bit-color path keeps these low levels
        // smooth. The S2's 13-bit PWM keeps the 1–10% / 3 s sweep set above.
        pat.breathe.min_pct   = 1;
        pat.breathe.max_pct   = 5;
        pat.breathe.period_ms = 5000;
    }
    return bb_led_anim_set_transition(s_anim, &pat, 800);
}

// Flash at a brightness level (OTA "updating"). Uses the animator's BLINK-with-
// level so it keeps flashing off the bb_timer while the miner is paused. On RGB
// LEDs the flash is blue; on the S2's single-channel PWM it's the duty.
bb_err_t led_blink(uint8_t level_pct, uint32_t period_ms) {
    if (!s_anim) return BB_OK;
    bb_led_anim_resume(s_anim);
    if (bb_led_caps(s_led) & BB_LED_CAP_RGB) {
        bb_led_set_color(s_led, 0, 0, 0, 255);
        // APA102 blue reads bright; drop the flash level hard on the RGB LED.
        level_pct = (uint8_t)(level_pct / 7);
    }
    bb_led_anim_pattern_t pat = {
        .kind  = BB_ANIM_BLINK,
        .blink = { .period_ms = period_ms, .duty_pct = 50, .level_pct = level_pct },
    };
    return bb_led_anim_set(s_anim, &pat);
}

#endif /* ESP_PLATFORM */
