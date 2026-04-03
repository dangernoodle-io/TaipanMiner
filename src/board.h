#pragma once

// Firmware version (updated by build/release process)
#define STICKMINER_VERSION "0.1.0"

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

// APA102 RGB LED (SPI bit-bang)
#define PIN_LED_CLK   39
#define PIN_LED_DIN   40

// BOOT button
#define PIN_BOOT_BTN  0

// Display states
#define DISP_STATE_NORMAL  0
#define DISP_STATE_FLIPPED 1
#define DISP_STATE_OFF     2
