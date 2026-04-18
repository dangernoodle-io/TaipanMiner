#pragma once
#if defined(ASIC_BM1370) || defined(ASIC_BM1368)

#include "esp_err.h"
#include "driver/i2c_master.h"

// Initialize EMC2101 for external diode temp + fan PWM.
esp_err_t emc2101_init(i2c_master_bus_handle_t bus, uint8_t addr);

// Read external diode temperature in degrees Celsius.
esp_err_t emc2101_read_temp(float *temp_c);

// Set fan PWM duty cycle (0-63).
esp_err_t emc2101_set_fan_duty(uint8_t duty_0_63);

// Read fan speed in RPM. Returns -1 on I2C error or stalled fan.
int emc2101_read_rpm(void);

#endif // ASIC_BM1370 || ASIC_BM1368
