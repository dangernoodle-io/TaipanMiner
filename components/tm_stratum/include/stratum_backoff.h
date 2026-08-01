#pragma once

#include <stdbool.h>
#include <stdint.h>

// TA-232: cap lowered from 60s to 30s, and the reset call site moved to fire
// ONLY on a successful stratum handshake (mining.authorize accepted) rather
// than on a bare TCP connect -- see stratum_fsm.c's action_enter_running.
#define STRATUM_BACKOFF_INITIAL_MS    5000U
#define STRATUM_BACKOFF_CAP_MS        30000U

typedef struct {
    uint32_t sleep_ms;     // how long the caller should sleep before retry
} stratum_backoff_step_t;

typedef struct {
    uint32_t delay_ms;
    int      fail_count;
} stratum_backoff_t;

void stratum_backoff_init(stratum_backoff_t *b);
void stratum_backoff_reset(stratum_backoff_t *b);    // successful handshake: delay_ms = INITIAL, fail_count = 0
stratum_backoff_step_t stratum_backoff_on_fail(stratum_backoff_t *b);
