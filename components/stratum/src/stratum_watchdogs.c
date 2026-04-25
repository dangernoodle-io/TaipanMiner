#include "stratum_watchdogs.h"

bool stratum_watchdog_job_drought(uint32_t now_ms, uint32_t last_pool_job_ms)
{
    if (last_pool_job_ms == 0) return false;
    return (uint32_t)(now_ms - last_pool_job_ms) >= STRATUM_WATCHDOG_JOB_DROUGHT_MS;
}

bool stratum_watchdog_share_drought(uint32_t now_ms,
                                    uint32_t last_share_ms,
                                    uint32_t session_start_ms)
{
    uint32_t ref = last_share_ms ? last_share_ms : session_start_ms;
    if (ref == 0) return false;
    return (uint32_t)(now_ms - ref) >= STRATUM_WATCHDOG_SHARE_DROUGHT_MS;
}

bool stratum_watchdog_needs_keepalive(uint32_t now_ms, uint32_t last_tx_ms)
{
    if (last_tx_ms == 0) return false;
    return (uint32_t)(now_ms - last_tx_ms) >= STRATUM_WATCHDOG_KEEPALIVE_MS;
}
