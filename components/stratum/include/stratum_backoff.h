#pragma once

#include <stdbool.h>
#include <stdint.h>

#define STRATUM_BACKOFF_INITIAL_MS    5000U
#define STRATUM_BACKOFF_CAP_MS        60000U
#define STRATUM_BACKOFF_KICK_THRESHOLD  5

typedef enum {
    STRATUM_BACKOFF_OUTCOME_BUMP = 0,  // bump fail_count, double delay (capped)
    STRATUM_BACKOFF_OUTCOME_KICK,      // threshold reached → reset + caller should kick WiFi
} stratum_backoff_outcome_t;

typedef struct {
    stratum_backoff_outcome_t outcome;
    uint32_t sleep_ms;     // how long the caller should sleep before retry
} stratum_backoff_step_t;

typedef struct {
    uint32_t delay_ms;
    int      fail_count;
} stratum_backoff_t;

void stratum_backoff_init(stratum_backoff_t *b);
void stratum_backoff_reset(stratum_backoff_t *b);    // success path: delay_ms = INITIAL, fail_count = 0
stratum_backoff_step_t stratum_backoff_on_fail(stratum_backoff_t *b);
