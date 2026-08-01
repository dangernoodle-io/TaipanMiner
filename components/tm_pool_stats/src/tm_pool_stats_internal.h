#pragma once

// tm_pool_stats -- private, component-internal helpers shared between
// tm_pool_stats.c and tm_pool_scoreboard.c. NOT installed under include/ --
// this header is never part of the public API surface.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Raw-bits NaN/inf/sign check for a diff-like double, shared by both the
// lifetime record's sanitizer (tm_pool_stats.c's tm_pool_stats_sanitize_slot)
// and the scoreboard's (tm_pool_scoreboard.c's tm_pool_scoreboard_sanitize).
// Avoids isnan()/isinf() to sidestep GCC FP-instrumentation phantom-branch
// coverage artifacts -- rationale intentionally duplicated (as a comment,
// not code) from v1's components/mining/src/mining_pool_stats.c, which this
// component does NOT depend on (v1 is dying wholesale at the v2 cutover).
static inline bool tm_pool_stats_diff_is_sane(double x)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));
    return (bits & UINT64_C(0x7FF0000000000000)) != UINT64_C(0x7FF0000000000000)
        && (bits >> 63) == 0;
}
