#pragma once

#ifdef ASIC_BM1370

#include "esp_err.h"

// Initialize ASIC hardware (UART, I2C, BM1370 init sequence).
// Call before starting asic_mining_task.
esp_err_t asic_init(void);

// ASIC mining task — dispatches work to BM1370, collects nonces.
void asic_mining_task(void *arg);

#endif // ASIC_BM1370
