#pragma once

#include "bb_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// tm_compose -- the first live-mining composition (TA-562):
// creates the two bb_bqueue work/result queues, binds tm_mining's
// mining_queue_ops_t and tm_pool_client's tm_pool_work_ops_t to the SAME
// pair of queues, and -- iff the FSM-selected active pool slot is
// configured -- spawns the mining task and the stratum service task and
// drives tm_stratum's V1 pool-client adapter against the active slot's
// tm_pool_cfg_t. A missing/unconfigured pool is not an init error: the
// queues + mining ops are still bound (mining idles on an empty peek) but
// no tasks are spawned and no pool client is initialized.
//
// Returns BB_OK on the unconfigured-slot early return, on a fully-wired
// configured-slot bring-up, and on host (where the bb_task_create/pool-
// client-init block is compiled out). Returns the first genuine failure
// (bb_bqueue_create, tm_pool_config_get, the pool client's own init(), or a
// bb_task_create() spawn failure) otherwise -- a boot-time wiring failure is
// never silently swallowed.
//
// Partial-spawn note: bb_task has no destroy/delete API for a DYNAMIC-backed
// task (bb_task_deregister() only removes the base-registry bookkeeping
// entry -- its own doc comment is explicit that it does NOT delete the
// FreeRTOS task; see bb_task.h). If the stratum-service bb_task_create()
// succeeds but the following mining bb_task_create() then fails, this
// function returns the error but the already-spawned stratum service task
// is left running (servicing a pipeline with no mining task feeding it
// results). This is an accepted fail-loud tradeoff, not a silent-limp one:
// a bb_task_create() failure at boot is effectively OOM, which is already
// fatal to the device's usefulness, and the returned error still surfaces
// the failure (rather than masking it) for a boot-time diagnosis.
bb_err_t tm_compose_mining_stratum_init(void);

#ifdef TM_COMPOSE_MINING_STRATUM_TESTING
// Test-only accessors (host tests only) -- expose just enough internal
// state to verify the adapter-to-queue mapping without duplicating
// tm_compose_mining_stratum.c's production logic in the test itself.
#include "tm_pool_work_seam.h"

// Resets all internal static state (destroys the work/result queues if
// created, zeroes the pool-client ctx/cfg/ops) so the next
// tm_compose_mining_stratum_init() call starts from a clean slate. Test
// teardown only.
void tm_compose_mining_stratum_test_reset(void);

// The tm_pool_work_ops_t bound to the wire's queues by the last
// tm_compose_mining_stratum_init() call that reached the configured-slot path
// (all-NULL if init() took the unconfigured-slot early return, or was never
// called since the last reset). Lets a test round-trip
// publish()->peek_work() and post_result()->drain() through the ACTUAL
// bound adapters, proving they target the same two queues
// mining_queue_ops_t does.
const tm_pool_work_ops_t *tm_compose_mining_stratum_test_work_ops(void);
#endif /* TM_COMPOSE_MINING_STRATUM_TESTING */

#ifdef __cplusplus
}
#endif
