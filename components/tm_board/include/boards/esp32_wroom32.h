#pragma once

/* ESP32-D0 (WROOM-32) — headless Phase 1 skeleton, no display/LED wired yet. */

#define BOARD_NAME "esp32-wroom32"

// No display — -1 pins satisfy bb_hw's board-header contract for boards
// without a panel.
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
