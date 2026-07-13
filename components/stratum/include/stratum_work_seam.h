#pragma once

#include <stdbool.h>
#include "stratum_job.h"

// Narrow seam between the stratum FSM and the cross-core work/result
// exchange. TA-562 (gated on breadboard's bb_bqueue, B1-821 -- not built
// yet) owns the REAL production implementation:
//   - work_publish  -> a capacity-1 "latest value" mailbox (xQueueOverwrite
//                      today; two concurrent peekers, never drained)
//   - result_drain  -> a capacity-16 bounded MPSC (two producers, one
//                      draining consumer)
//
// This ticket (TA-560) defines the SEAM ONLY: stratum_fsm.c calls these two
// function pointers and never touches a FreeRTOS queue, bb_bqueue, or any
// other transport directly. A host/test fake is trivial to construct (see
// test_stratum_fsm.c) by pointing the ops struct at plain capture callbacks.
// Do NOT hand-roll a FreeRTOS queue here as a "production" stand-in -- that
// is exactly the premature binding TA-562 exists to avoid.

// Publish a freshly-parsed pool job (mining.notify). Implementations MUST
// be non-blocking / bounded -- called from the stratum FSM's own task.
typedef bool (*stratum_work_publish_fn)(void *ctx, const stratum_job_t *job);

// Drain one submitted mining result, if any is pending. Returns true and
// fills *out if a result was available, false if the queue is empty.
// Implementations MUST be non-blocking (zero-timeout poll).
typedef struct {
    char     job_id[64];
    char     extranonce2_hex[17];
    char     ntime_hex[9];
    char     nonce_hex[9];
    char     version_hex[9];   // empty string if version-rolling not used
    double   share_diff;
} stratum_result_t;

typedef bool (*stratum_result_drain_fn)(void *ctx, stratum_result_t *out);

// Reset/drain-all hook, called once on session teardown (TA-530 single
// funnel) so stale work/results from the previous session never leak into
// the next one. Optional -- NULL is a valid no-op.
typedef void (*stratum_work_reset_fn)(void *ctx);

typedef struct {
    void                    *ctx;
    stratum_work_publish_fn  publish;
    stratum_result_drain_fn  drain;
    stratum_work_reset_fn    reset;
} stratum_work_ops_t;
