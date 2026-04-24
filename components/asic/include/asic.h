#pragma once

#include "asic_chip.h"
#include "bb_nv.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef ASIC_CHIP

// ASIC chip operations abstraction
typedef struct {
    bb_err_t (*chip_init)(void);
    esp_err_t (*chip_quiesce)(void);   // send CMD_INACTIVE to halt hashing
    esp_err_t (*chip_resume)(void);    // re-run chip init after pause
    bb_err_t (*vreg_init)(i2c_master_bus_handle_t bus, uint16_t target_mv);
    uint16_t fb_min;
    uint16_t fb_max;
    uint16_t default_mv;
    uint16_t default_freq_mhz;
    uint8_t  chip_count;
    uint16_t chip_id;
    uint32_t job_interval_ms;
} asic_chip_ops_t;

extern const asic_chip_ops_t *g_chip_ops;

// Initialize ASIC hardware (UART, I2C, chip init sequence).
// Call before starting asic_mining_task.
bb_err_t asic_init(void);

// ASIC mining task — dispatches work to ASIC, collects nonces.
void asic_mining_task(void *arg);

// Get I2C bus handle (for display and other peripherals on the bus).
i2c_master_bus_handle_t asic_get_i2c_bus(void);

// Set I2C bus handle (for unprovisioned mode or display fallback).
void asic_set_i2c_bus(i2c_master_bus_handle_t bus);

#endif // ASIC_CHIP
