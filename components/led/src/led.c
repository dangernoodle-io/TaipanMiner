#include "led.h"

#ifdef ESP_PLATFORM

#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include <string.h>

#if defined(BOARD_TDONGLE_S3)

static const char *TAG = "led";

// File-scope static variables for GPIO handles
static gpio_num_t s_clk_pin;
static gpio_num_t s_din_pin;

// Bit-bang APA102 SPI protocol
// MSB first, clock idle low, data sampled on rising edge
static void s_write_bit(bool bit) {
    // Setup data line for this bit
    gpio_set_level(s_din_pin, bit ? 1 : 0);
    // Rising edge samples data
    gpio_set_level(s_clk_pin, 1);
    gpio_set_level(s_clk_pin, 0);
}

// Write 8 bits, MSB first
static void s_write_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        s_write_bit((byte >> i) & 1);
    }
}

// Write complete APA102 frame
static void s_write_apa102_frame(uint8_t r, uint8_t g, uint8_t b) {
    // Start frame: 4 bytes of 0x00
    s_write_byte(0x00);
    s_write_byte(0x00);
    s_write_byte(0x00);
    s_write_byte(0x00);

    // LED frame: brightness (0xE0 | 0x1F) + B + G + R
    s_write_byte(0xFF);  // Full brightness (0xE0 | 0x1F = 0xFF)
    s_write_byte(b);
    s_write_byte(g);
    s_write_byte(r);

    // End frame: 4 bytes of 0xFF
    s_write_byte(0xFF);
    s_write_byte(0xFF);
    s_write_byte(0xFF);
    s_write_byte(0xFF);
}

bb_err_t led_init(void) {
    s_clk_pin = PIN_LED_CLK;
    s_din_pin = PIN_LED_DIN;

    // Configure GPIO outputs
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << s_clk_pin) | (1ULL << s_din_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "failed to configure LED GPIO");

    // Set both lines low initially
    ESP_RETURN_ON_ERROR(gpio_set_level(s_clk_pin, 0), TAG, "failed to set CLK low");
    ESP_RETURN_ON_ERROR(gpio_set_level(s_din_pin, 0), TAG, "failed to set DIN low");

    return BB_OK;
}

bb_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    s_write_apa102_frame(r, g, b);
    return BB_OK;
}

bb_err_t led_off(void) {
    s_write_apa102_frame(0, 0, 0);
    return BB_OK;
}

#else

// BOARD_BITAXE_601 and unknown boards: no-op stubs

bb_err_t led_init(void) {
    return BB_OK;
}

bb_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    (void)r;
    (void)g;
    (void)b;
    return BB_OK;
}

bb_err_t led_off(void) {
    return BB_OK;
}

#endif

#endif
