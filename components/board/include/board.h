#pragma once

#include "bb_hw.h"

#if defined(BOARD_TDONGLE_S3) || defined(BOARD_BITAXE_601) || defined(BOARD_BITAXE_403) || defined(BOARD_BITAXE_650)
#  define HAS_I2C 1
#endif

// BOARD_ESP32_WROOM32_CYD: ILI9341 320x240 TFT via SPI; no I2C; active-low RGB LED.
// PIN_I2C_SDA/SCL are -1 in the board header — no HAS_I2C defined.

#if CONFIG_IDF_TARGET_ESP32C3 && defined(HAS_I2C)
#  if (PIN_I2C_SDA > 21) || (PIN_I2C_SCL > 21)
#    error "I2C pins out of range for ESP32-C3 (max GPIO 21)"
#  endif
#endif
