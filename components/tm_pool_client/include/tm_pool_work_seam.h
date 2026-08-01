#pragma once

#include <stdbool.h>
#include "work.h"     // mining_work_t
#include "mining.h"   // mining_result_t

// Narrow seam between a pool-client backend (tm_stratum's V1 adapter today,
// a future SV2 backend later) and the cross-core work/result exchange. This
// is tm_pool_client's own seam -- protocol-agnostic, built entirely from
// tm_mining's own types, with zero stratum-wire content -- not something a
// specific protocol backend should own (moved out of tm_stratum, TA-571
// firmware review finding #1: it was the one thing forcing
// tm_pool_client<->tm_stratum to REQUIRES each other).
//
// TA-562 (gated on breadboard's bb_bqueue, B1-821 -- not built yet) owns the
// REAL production implementation:
//   - work_publish  -> a capacity-1 "latest value" mailbox (xQueueOverwrite
//                      today; two concurrent peekers, never drained)
//   - result_drain  -> a capacity-16 bounded MPSC (two producers, one
//                      draining consumer)
//
// This component defines the SEAM ONLY: a pool-client backend calls these
// two function pointers and never touches a FreeRTOS queue, bb_bqueue, or
// any other transport directly. A host/test fake is trivial to construct
// (see test_stratum_fsm.c) by pointing the ops struct at plain capture
// callbacks. Do NOT hand-roll a FreeRTOS queue here as a "production"
// stand-in -- that is exactly the premature binding TA-562 exists to avoid.
//
// The published/drained types are tm_mining's own mining_work_t/
// mining_result_t (not a duplicate) -- tm_stratum's V1 adapter builds a
// ready-to-hash mining_work_t via build_work() (work_build.h) and hands it
// across this seam directly.

// Publish a freshly-built, ready-to-hash job. Implementations MUST be
// non-blocking / bounded -- called from the pool-client backend's own task.
typedef bool (*tm_pool_work_publish_fn)(void *ctx, const mining_work_t *work);

// Drain one submitted mining result, if any is pending. Returns true and
// fills *out if a result was available, false if the queue is empty.
// Implementations MUST be non-blocking (zero-timeout poll).
typedef bool (*tm_pool_result_drain_fn)(void *ctx, mining_result_t *out);

// Reset/drain-all hook, called once on session teardown (e.g. tm_stratum's
// TA-530 single funnel) so stale work/results from the previous session
// never leak into the next one. Optional -- NULL is a valid no-op.
typedef void (*tm_pool_work_reset_fn)(void *ctx);

typedef struct {
    void                     *ctx;
    tm_pool_work_publish_fn   publish;
    tm_pool_result_drain_fn   drain;
    tm_pool_work_reset_fn     reset;
} tm_pool_work_ops_t;
