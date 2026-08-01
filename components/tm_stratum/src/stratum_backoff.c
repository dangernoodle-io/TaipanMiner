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
    // Caller will sleep current delay; pre-double for next time (capped).
    step.sleep_ms = b->delay_ms;
    if (b->delay_ms < STRATUM_BACKOFF_CAP_MS) {
        b->delay_ms *= 2;
        if (b->delay_ms > STRATUM_BACKOFF_CAP_MS) b->delay_ms = STRATUM_BACKOFF_CAP_MS;
    }
    return step;
}
