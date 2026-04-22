#pragma once

#include "bb_hw.h"

#if defined(BOARD_TDONGLE_S3) || defined(BOARD_BITAXE_601) || defined(BOARD_BITAXE_403)
#  define HAS_I2C 1
#endif

#if CONFIG_IDF_TARGET_ESP32C3 && defined(HAS_I2C)
#  if (PIN_I2C_SDA > 21) || (PIN_I2C_SCL > 21)
#    error "I2C pins out of range for ESP32-C3 (max GPIO 21)"
#  endif
#endif
