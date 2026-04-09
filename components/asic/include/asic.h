#pragma once

#ifdef ASIC_BM1370

#include "esp_err.h"
#include "driver/i2c_master.h"

// Initialize ASIC hardware (UART, I2C, BM1370 init sequence).
// Call before starting asic_mining_task.
esp_err_t asic_init(void);

// ASIC mining task — dispatches work to BM1370, collects nonces.
void asic_mining_task(void *arg);

// Get I2C bus handle (for display and other peripherals on the bus).
i2c_master_bus_handle_t asic_get_i2c_bus(void);

// Set I2C bus handle (for unprovisioned mode or display fallback).
void asic_set_i2c_bus(i2c_master_bus_handle_t bus);

#endif // ASIC_BM1370
