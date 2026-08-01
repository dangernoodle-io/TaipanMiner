#pragma once

// Shared tm_pool_stats host-test helpers -- used by both
// test_tm_pool_stats.c and test_tm_pool_scoreboard.c. Header-only,
// static-inline (mirrors fake_nvs_backend.h's pattern): each TU that
// includes this gets its own private copy, no cross-TU sharing needed.

#include "tm_pool_stats.h"

#include <string.h>

// Minimal tm_pool_share_t builder -- diff/submitted_ts only; every other
// field defaults to 0/NULL/false. Tests that need hash/extranonce2/job_id
// populate those fields directly on the returned struct.
static inline tm_pool_share_t tm_pool_test_share(double diff, int64_t submitted_ts)
{
    tm_pool_share_t share;
    memset(&share, 0, sizeof(share));
    share.diff         = diff;
    share.submitted_ts = submitted_ts;
    return share;
}
