#include "stratum_backoff.h"

void stratum_backoff_init(stratum_backoff_t *b)
{
    b->delay_ms = STRATUM_BACKOFF_INITIAL_MS;
    b->fail_count = 0;
}

void stratum_backoff_reset(stratum_backoff_t *b)
{
    b->delay_ms = STRATUM_BACKOFF_INITIAL_MS;
    b->fail_count = 0;
}

stratum_backoff_step_t stratum_backoff_on_fail(stratum_backoff_t *b)
{
    stratum_backoff_step_t step;
    b->fail_count++;
    if (b->fail_count >= STRATUM_BACKOFF_KICK_THRESHOLD) {
        // Reset state for the post-kick reconnect attempt; caller fires the kick.
        b->fail_count = 0;
        b->delay_ms = STRATUM_BACKOFF_INITIAL_MS;
        step.outcome = STRATUM_BACKOFF_OUTCOME_KICK;
        step.sleep_ms = STRATUM_BACKOFF_INITIAL_MS;
        return step;
    }
    // Caller will sleep current delay; pre-double for next time (capped).
    step.outcome = STRATUM_BACKOFF_OUTCOME_BUMP;
    step.sleep_ms = b->delay_ms;
    if (b->delay_ms < STRATUM_BACKOFF_CAP_MS) {
        b->delay_ms *= 2;
        if (b->delay_ms > STRATUM_BACKOFF_CAP_MS) b->delay_ms = STRATUM_BACKOFF_CAP_MS;
    }
    return step;
}
