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

// Mining task — runs on Core 1, priority 20
void mining_task(void *arg);

// Software SHA mining task — runs on Core 0, priority 3
#ifdef STICKMINER_DEBUG
void mining_task_sw(void *arg);
#endif
