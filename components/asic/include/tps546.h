#pragma once
#if defined(ASIC_BM1370) || defined(ASIC_BM1368)

#include "esp_err.h"
#include "driver/i2c_master.h"

// Initialize TPS546029 and power on at target_mv millivolts.
// Reads VOUT_MODE exponent, encodes voltage as ULINEAR16, powers on.
esp_err_t tps546_init(i2c_master_bus_handle_t bus, uint8_t addr, uint16_t target_mv);

// Change output voltage (after init).
esp_err_t tps546_set_voltage_mv(uint16_t target_mv);

#endif // ASIC_BM1370 || ASIC_BM1368
