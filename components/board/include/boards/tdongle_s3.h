#pragma once

#define BOARD_NAME "tdongle-s3"
#define BOARD_HAS_DISPLAY 1
#define BOARD_DISPLAY_PANEL_ST77XX 1

// ST7735 LCD (SPI)
#define PIN_LCD_CLK   5
#define PIN_LCD_MOSI  3
#define PIN_LCD_CS    4
#define PIN_LCD_DC    2
#define PIN_LCD_RST   1
#define PIN_LCD_BL    38

// LCD dimensions (landscape)
#define LCD_WIDTH     160
#define LCD_HEIGHT    80
#define LCD_OFFSET_X  1
#define LCD_OFFSET_Y  26

// I2C — not wired on T-Dongle S3; -1 satisfies bb_display_ssd1306 compile
// (probe will fail gracefully at runtime and the backend is skipped).
#define PIN_I2C_SDA    -1
#define PIN_I2C_SCL    -1
#define I2C_BUS_SPEED_HZ 400000
#define I2C_BUS_NUM    0

// APA102 RGB LED (SPI bit-bang)
#define PIN_LED_CLK   39
#define PIN_LED_DIN   40

// BOOT button
#define PIN_BOOT_BTN  0

// Display states
#define DISP_STATE_NORMAL  0
#define DISP_STATE_FLIPPED 1
#define DISP_STATE_OFF     2
