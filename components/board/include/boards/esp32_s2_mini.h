#pragma once

/* ESP32-S2 Mini — headless SW-mining scaffold. Single-core; miner shares core 0.
 * OLED support not planned for this variant. TA-scaffold. */

#define BOARD_NAME "esp32-s2-mini"

// Single-core, no-PSRAM, serial-less in run mode: in-place pull-OTA can't clear
// the TLS+OTA buffers under the fragmented runtime heap. Opt into breadboard's
// OTA-only boot mode (bb_ota_boot) — apply arms a flag + reboots, then the
// firmware pulls at full early-boot heap before any subsystem starts.
#define BOARD_OTA_BOOT_MODE

// Onboard LED (LOLIN S2 Mini) — GPIO 15. Single GPIO driven on/off by the led
// component's S2 path; gives the headless board OTA visual feedback (on=updating,
// off=idle/done) via the shared bb_ota_progress_cb_t.
#define PIN_STATUS_LED  15

// BOOT button
#define PIN_BOOT_BTN  0

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
