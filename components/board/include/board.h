#pragma once

#if defined(BOARD_TDONGLE_S3)
#  include "boards/tdongle_s3.h"
#elif defined(BOARD_BITAXE_601)
#  include "boards/bitaxe_601.h"
#elif defined(BOARD_BITAXE_403)
#  include "boards/bitaxe_403.h"
#else
#  error "Unknown board — add -DBOARD_xxx to build_flags"
#endif

// I2C feature: enabled for boards with validated I2C pin assignments
#if defined(BOARD_BITAXE_601) || defined(BOARD_BITAXE_403) || defined(BOARD_TDONGLE_S3)
#  define HAS_I2C 1
#endif
// N8-T intentionally omitted until real I2C pins are confirmed (TA-48)

// Compile-time sanity check: C3 targets must have I2C pins in valid range
#if CONFIG_IDF_TARGET_ESP32C3 && defined(HAS_I2C)
#  if (PIN_I2C_SDA > 21) || (PIN_I2C_SCL > 21)
#    error "I2C pins out of range for ESP32-C3 (max GPIO 21)"
#  endif
#endif
