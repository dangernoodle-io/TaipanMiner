#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Mining result sent from mining task to stratum task
typedef struct {
    char     job_id[64];
    char     extranonce2_hex[17];
    char     ntime_hex[9];
    char     nonce_hex[9];
    char     version_hex[9];   // BIP 320: rolled version as hex; empty if no rolling
} mining_result_t;

// Queues (created by main, used by stratum + mining tasks)
extern QueueHandle_t work_queue;
extern QueueHandle_t result_queue;

#ifdef ESP_PLATFORM
#include "freertos/task.h"
#include "freertos/semphr.h"

// Mining task handles (for suspend/resume during OTA verification)
extern TaskHandle_t mining_hw_task_handle;
extern TaskHandle_t mining_sw_task_handle;

// Shared hashrate stats (updated by both mining tasks, read for logging)
typedef struct {
    double hw_hashrate;    // latest HW hashrate (H/s)
    double sw_hashrate;    // latest SW hashrate (H/s)
    uint32_t hw_shares;    // hardware shares found
    uint32_t sw_shares;    // software shares found
    SemaphoreHandle_t mutex;
} mining_stats_t;

extern mining_stats_t mining_stats;

// Initialize mining stats mutex. Call once from main before starting tasks.
void mining_stats_init(void);
#endif

// Mining task — runs on Core 1, priority 20
void mining_task(void *arg);

// Software SHA mining task — runs on Core 0, priority 3
void mining_task_sw(void *arg);
