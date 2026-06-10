#pragma once

/* Elegoo ESP32-2432S028R ("CYD" — Cheap Yellow Display).
 * ESP32-WROOM-32E, 4 MB flash, ILI9341 320x240 TFT (SPI), XPT2046 touch (unused).
 * Standard single-micro-USB revision. TA-CYD. */

#define BOARD_NAME "esp32-wroom32-cyd"
#define BOARD_HAS_DISPLAY 1
#define BOARD_DISPLAY_PANEL_ILI9341 1

// ILI9341 TFT — SPI bus, landscape 320x240
#define PIN_LCD_MOSI   13
#define PIN_LCD_MISO   12
#define PIN_LCD_CLK    14
#define PIN_LCD_CS     15
#define PIN_LCD_DC      2
#define PIN_LCD_RST    -1   // no dedicated reset line on this board
#define PIN_LCD_BL     21   // backlight active-HIGH (see sdkconfig/esp32-wroom32-cyd)

#define LCD_WIDTH      320
#define LCD_HEIGHT     240
#define LCD_OFFSET_X     0
#define LCD_OFFSET_Y     0

// BOOT button
#define PIN_BOOT_BTN    0

// I2C — not present on CYD; -1 satisfies compile requirements
#define PIN_I2C_SDA    -1
#define PIN_I2C_SCL    -1
#define I2C_BUS_SPEED_HZ 400000
#define I2C_BUS_NUM    0

// RGB LED (common-anode, active-LOW) — Elegoo 2432S028R '-32E' variant:
// R=GPIO22, G=GPIO16, B=GPIO17.
// NOTE: differs from standard CYD (which uses R=GPIO4); GPIO4 is unused on this board.
// Verified by hardware sweep. Status/heartbeat LED uses RED (GPIO22).
// bb_led_pwm drives PIN_STATUS_LED with active_low=true (driving LOW turns LED on).
// Unused channels (G=16, B=17) are driven HIGH (off) in led_backend_open to prevent
// common-anode float-on.
#define PIN_STATUS_LED 22   // red channel, active-low
#define PIN_LED_R      22   // red channel, active-low
#define PIN_LED_G      16   // green channel, active-low
#define PIN_LED_B      17   // blue channel, active-low

// XPT2046 touch (document only — no driver in firmware)
// T_CLK=25, T_CS=33, T_MOSI=32, T_MISO=39, T_IRQ=36
