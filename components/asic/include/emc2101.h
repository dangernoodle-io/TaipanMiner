#pragma once
#if defined(ASIC_BM1370) || defined(ASIC_BM1368)

#include "bb_nv.h"
#include "driver/i2c_master.h"

// Initialize EMC2101 for external diode temp + fan PWM.
bb_err_t emc2101_init(i2c_master_bus_handle_t bus, uint8_t addr);

// Read external diode temperature (ASIC die, via BM1370's temp diode) in degrees Celsius.
bb_err_t emc2101_read_temp(float *temp_c);

// Read EMC2101 internal (chip) temperature in degrees Celsius.
// Reflects board/ambient temp near the fan controller (1 degC resolution).
bb_err_t emc2101_read_internal_temp(float *temp_c);

// Set fan PWM duty cycle (0-63).
bb_err_t emc2101_set_fan_duty(uint8_t duty_0_63);

// Set fan duty as a percentage [0, 100]. Clamped. Caches the value for emc2101_get_duty_pct().
void emc2101_set_duty_pct(int pct);

// Return the last duty percentage set via emc2101_set_duty_pct(). Returns -1 if not yet set.
int emc2101_get_duty_pct(void);

// Read fan speed in RPM. Returns -1 on I2C error or stalled fan.
int emc2101_read_rpm(void);

#endif // ASIC_BM1370 || ASIC_BM1368
