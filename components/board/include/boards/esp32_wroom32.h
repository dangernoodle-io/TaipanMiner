#pragma once

/* ESP32-D0 (ELEGOO WROOM-32) — headless no-ASIC SW-mining variant. TA-271. */

#define BOARD_NAME "esp32-wroom32"

// BOOT button
#define PIN_BOOT_BTN  0

// Onboard status LED — active-high LED on GPIO2 on the DevKit. Single-channel,
// driven via bb_led_pwm (same boot-solid → breathe heartbeat path as the S2).
#define PIN_STATUS_LED 2

// No display — -1 pins satisfy bb_display_st77xx and bb_display_ssd1306
// compile requirements; both backends' probe/init will return early.
#define PIN_LCD_CLK    -1
#define PIN_LCD_MOSI   -1
#define PIN_LCD_CS     -1
#define PIN_LCD_DC     -1
#define PIN_LCD_RST    -1
#define PIN_LCD_BL     -1
#define LCD_WIDTH       0
#define LCD_HEIGHT      0
#define LCD_OFFSET_X    0
#define LCD_OFFSET_Y    0
#define PIN_I2C_SDA    -1
#define PIN_I2C_SCL    -1
#define I2C_BUS_SPEED_HZ 400000
#define I2C_BUS_NUM    0
