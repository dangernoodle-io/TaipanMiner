#pragma once
#include "asic_chip.h"
#ifdef ASIC_CHIP

#include "bb_nv.h"
#include "driver/i2c_master.h"

// Initialize TPS546029 and power on at target_mv millivolts.
// Reads VOUT_MODE exponent, encodes voltage as ULINEAR16, powers on.
bb_err_t tps546_init(i2c_master_bus_handle_t bus, uint8_t addr, uint16_t target_mv);

// Change output voltage (after init).
bb_err_t tps546_set_voltage_mv(uint16_t target_mv);

// Read output voltage. Returns mV, or -1 on I2C error.
int tps546_read_vout_mv(void);

// Read output current. Returns mA, or -1 on I2C error.
int tps546_read_iout_ma(void);

// Read input voltage. Returns mV, or -1 on I2C error.
int tps546_read_vin_mv(void);

// Read voltage regulator temperature. Returns degC as int, or -1 on I2C error.
int tps546_read_temp_c(void);

#endif // ASIC_BM1370 || ASIC_BM1368
