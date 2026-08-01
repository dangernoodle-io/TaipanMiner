#pragma once

// tm_pool_policy -- the pool SELECTION engine (TM v2 pool subsystem PR5,
// TA-572/TA-203 auto-rotation). Companion to tm_pool_config (settings) and
// tm_pool_stats (lifetime counters): this component owns the decision of
// WHICH configured slot is active and drives a tm_pool_client_ops_t (PR4,
// tm_pool_client.h) to reconnect when that decision changes. It owns NO
// transport/FSM state of its own -- every reconnect/failure signal flows
// through the ops vtable passed into tm_pool_policy_tick().
//
// Three selection modes:
//   MANUAL       -- active_idx always tracks the persisted manual_idx;
//                   any external change (via tm_pool_policy_set_manual_idx
//                   or tm_pool_policy_set_mode) reconnects on the NEXT tick.
//   FAILOVER     -- advances to the next CONFIGURED slot (skipping unset
//                   slots, wrapping) after TM_POOL_FAILOVER_THRESHOLD
//                   consecutive tm_pool_client_ops_t.consumed_read_failure()
//                   observations. No auto-fail-back: a recovered prior slot
//                   is never automatically re-selected.
//   AUTO_ROTATE  -- advances round-robin across configured slots on either
//                   a wall-clock interval or an accepted-share-count
//                   threshold, whichever fires first; both accumulators
//                   reset together on any fire. Disabled (rotate_enabled=
//                   false) or both thresholds at 0 means neither trigger
//                   ever fires. The window/accumulator are also PRIMED
//                   (reset) the moment the mode is switched INTO
//                   AUTO_ROTATE from another mode -- see tm_pool_policy_
//                   tick()'s AUTO_ROTATE doc comment for why.
//
// Persisted config: bb_config field table, namespace "tm_pool" (the SAME
// namespace tm_pool_config/tm_pool_stats already use -- distinct keys, no
// collision: sel_mode/sel_manual_idx/rotate_en/rotate_hrs/rotate_shares/
// rotate_order, all well under the real NVS 15-char key limit). See
// src/tm_pool_policy.c.
//
// Runtime-only state (active_idx, fail_count, rotate accumulators/window)
// lives in RAM ONLY -- never persisted, always starts fresh on
// tm_pool_policy_init(). MANUAL's derived active_idx and AUTO_ROTATE's
// accumulators are cheap to recompute/re-accrue; persisting them would only
// invite a stale-vs-live desync across a reboot for no benefit.
//
// Clock: tm_pool_policy_tick()'s `now_ms` is caller-injected (same
// convention as tm_pool_client_ops_t.service() and stratum_fsm_service()) --
// this component never reads a clock internally, which is what makes its
// AUTO_ROTATE time-trigger logic directly host-testable via a fake clock,
// with no bb_timer/bb_clock dependency of its own.
//
// Concurrency: internally synchronized via bb_core's bb_lock (mirrors
// tm_pool_stats's discipline) -- callers need no external mutex.
// tm_pool_policy_tick() (a periodic driver task), tm_pool_policy_
// record_accepted_share() (the stratum/accept path, a different task), and
// the runtime setters (a config/HTTP path, yet another task) are all
// expected to run concurrently from different tasks; every entry point
// below acquires the same internal lock for its full duration.

#include "bb_core.h"
#include "tm_pool_client.h" // tm_pool_client_ops_t (tm_pool_policy_tick's driven vtable)

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TM_POOL_SEL_MANUAL      = 0,
    TM_POOL_SEL_FAILOVER    = 1,
    TM_POOL_SEL_AUTO_ROTATE = 2,
} tm_pool_sel_mode_t;

typedef struct {
    tm_pool_sel_mode_t mode;
    uint8_t            manual_idx;
    bool               rotate_enabled;
    uint32_t           rotate_interval_hours;  // 0 = time trigger disabled
    uint32_t           rotate_share_threshold; // 0 = share trigger disabled
    uint8_t            rotate_order;           // 0 = sequential (the only value defined today)
} tm_pool_policy_cfg_t;

// Consecutive consumed_read_failure() observations required to advance
// FAILOVER to the next configured slot. v1 parity constant.
#define TM_POOL_FAILOVER_THRESHOLD 3

// Composition-tier init: loads the persisted config and derives the initial
// active_idx from it (MANUAL -> manual_idx; FAILOVER/AUTO_ROTATE -> the
// first configured slot, scanning idx 0..TM_POOL_MAX-1, or 0 if nothing is
// configured yet). Runtime-only state (fail_count, rotate accumulators/
// window, and the internal MANUAL-mode change tracker) is reset fresh --
// never loaded from storage. Returns the first genuine bb_config/bb_storage
// read failure encountered while loading the persisted config, else BB_OK.
bb_err_t tm_pool_policy_init(void);

// The selection engine. Safe to call at a fixed cadence regardless of
// connection state (same contract as tm_pool_client_ops_t.service()) --
// `ops` and `client_ctx` identify the pool-client instance this tick drives;
// `now_ms` is the caller-injected clock (see the file-level doc comment).
// A NULL `ops` is a no-op. Per mode:
//   MANUAL      -- active_idx is set to the persisted manual_idx; if that
//                  differs from the value this component last synced to
//                  (i.e. it changed since the last tick that observed a
//                  change), ops->request_reconnect() fires exactly once.
//                  A stable manual_idx across ticks never reconnects.
//                  Short-circuits FAILOVER/AUTO_ROTATE entirely -- no
//                  failure/rotation bookkeeping happens while in this mode.
//   FAILOVER    -- if ops->consumed_read_failure() reports a fresh hard
//                  failure, increments an internal fail_count; at
//                  TM_POOL_FAILOVER_THRESHOLD, advances active_idx to the
//                  next CONFIGURED slot (tm_pool_config_is_configured(),
//                  skipping unset slots, wrapping), resets fail_count to 0,
//                  and calls ops->request_reconnect(). If no OTHER slot is
//                  configured, active_idx is left unchanged (there is
//                  nowhere to advance to) but fail_count still resets and
//                  request_reconnect() still fires -- reaching the
//                  threshold always requests one reconnect attempt, whether
//                  or not the active slot actually moved. No auto-fail-back.
//   AUTO_ROTATE -- on the FIRST AUTO_ROTATE tick after
//                  tm_pool_policy_set_mode(TM_POOL_SEL_AUTO_ROTATE) was
//                  called from a DIFFERENT prior mode, the window start and
//                  share accumulator are primed (reset) to this tick's
//                  now_ms/0 BEFORE any trigger is evaluated -- otherwise a
//                  device that spent hours/days in MANUAL/FAILOVER before
//                  switching would see now_ms - window_start already equal
//                  to its full uptime (an immediate, unexpected rotation),
//                  and any shares accepted under the prior mode (rotate_
//                  accum_shares is accrued regardless of mode -- see
//                  tm_pool_policy_record_accepted_share()'s doc comment)
//                  would instantly exceed a small share_threshold the same
//                  way. Priming makes the first interval/count always
//                  measured from mode ENTRY, never from boot or from
//                  accrual under a different mode. After priming (a one-
//                  time event per mode entry), if rotate_enabled, checks a
//                  time trigger (now_ms - window_start >=
//                  rotate_interval_hours*3600000, only when
//                  rotate_interval_hours > 0) and a share trigger
//                  (accumulated accepted shares >= rotate_share_threshold,
//                  only when rotate_share_threshold > 0). Either firing
//                  advances active_idx round-robin to the next configured
//                  slot, resets BOTH accumulators (accepted-share count and
//                  the time window's start), and calls
//                  ops->request_reconnect(). rotate_enabled=false, or both
//                  thresholds at 0, means neither trigger ever fires.
void tm_pool_policy_tick(const tm_pool_client_ops_t *ops, void *client_ctx, uint32_t now_ms);

// Increments the AUTO_ROTATE share-trigger accumulator. Composition calls
// this on every accepted share regardless of the current mode -- it is only
// ever CONSUMED by an AUTO_ROTATE tick, so accruing it in any other mode is
// always safe. Any accrual from a PRIOR mode is discarded the moment
// AUTO_ROTATE actually begins evaluating triggers (see tm_pool_policy_tick()'s
// AUTO_ROTATE priming doc comment) -- it does NOT carry over across a mode
// switch, so a pre-accumulated count under MANUAL/FAILOVER can never
// instantly trigger a rotation the moment AUTO_ROTATE is selected.
void tm_pool_policy_record_accepted_share(void);

// The currently active pool slot index, per the current mode's decision.
uint8_t tm_pool_policy_active_idx(void);

// Runtime setters: persist the new value AND update the in-RAM config so it
// takes effect on the VERY NEXT tick (no re-init needed). A manual override
// (tm_pool_policy_set_mode(TM_POOL_SEL_MANUAL) followed by
// tm_pool_policy_set_manual_idx()) always wins on the next tick regardless
// of what FAILOVER/AUTO_ROTATE had previously selected.
// tm_pool_policy_set_mode(TM_POOL_SEL_AUTO_ROTATE), when the CURRENT mode is
// something else, additionally arms the priming flag consumed by the first
// AUTO_ROTATE tick that follows (see tm_pool_policy_tick()'s AUTO_ROTATE doc
// comment) -- switching mode alone never touches now_ms (this function has
// none to prime with), so the actual window/accumulator reset is deferred to
// that first tick, which does.
//
// Returns:
//   BB_OK                 persisted and applied
//   BB_ERR_INVALID_ARG    tm_pool_policy_set_manual_idx: idx >= TM_POOL_MAX
//   (other)               a genuine bb_config/bb_storage write failure --
//                         the in-RAM config is left unchanged on failure
bb_err_t tm_pool_policy_set_mode(tm_pool_sel_mode_t mode);
bb_err_t tm_pool_policy_set_manual_idx(uint8_t idx);

// Persists all 4 rotate settings together, sequentially (enabled, interval,
// share threshold, order), stopping at the first failing field -- same
// early-return/partial-write contract tm_pool_config_set() documents. Each
// field that DID persist successfully is also applied to the in-RAM config
// before any later field's failure is returned, so RAM never disagrees with
// what storage actually holds.
bb_err_t tm_pool_policy_set_rotate_cfg(bool enabled, uint32_t interval_hours,
                                        uint32_t share_threshold, uint8_t order);

#ifdef __cplusplus
}
#endif
