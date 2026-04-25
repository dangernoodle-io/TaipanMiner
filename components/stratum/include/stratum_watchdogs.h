#pragma once

#include <stdbool.h>
#include <stdint.h>

#define STRATUM_WATCHDOG_JOB_DROUGHT_MS    (5UL * 60 * 1000)   // 5 min
#define STRATUM_WATCHDOG_SHARE_DROUGHT_MS  (30UL * 60 * 1000)  // 30 min
#define STRATUM_WATCHDOG_KEEPALIVE_MS      90000UL             // 90s

// All times are in milliseconds. A *_ms value of 0 means "never observed yet"
// — those branches do NOT trip (we don't reconnect a fresh session).
//
// Inputs are uint32_t to mirror the FreeRTOS tick semantics in the caller;
// the helpers do unsigned subtraction so single-wraparound is naturally
// correct. Callers that pass tick counts should convert to ms once via
// pdTICKS_TO_MS or equivalent before calling.

bool stratum_watchdog_job_drought(uint32_t now_ms, uint32_t last_pool_job_ms);

// Share drought uses last_share_ms if non-zero, else session_start_ms.
// If both are zero, returns false.
bool stratum_watchdog_share_drought(uint32_t now_ms,
                                    uint32_t last_share_ms,
                                    uint32_t session_start_ms);

bool stratum_watchdog_needs_keepalive(uint32_t now_ms, uint32_t last_tx_ms);
