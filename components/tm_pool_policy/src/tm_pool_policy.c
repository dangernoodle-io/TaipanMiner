// tm_pool_policy -- see include/tm_pool_policy.h for the public contract.
//
// Storage: 6 scalar bb_config fields, backend="nvs", namespace "tm_pool"
// (TM_POOL_POLICY_NVS_NS below -- the SAME namespace tm_pool_config/
// tm_pool_stats already use; distinct key names, no collision). Every field
// carries a declared default, so tm_pool_policy_init() always resolves a
// full config on first boot (no persisted value yet) without a single
// NOT_FOUND special case to handle.
//
// Concurrency: internally synchronized via bb_lock (s_lock) -- this
// component is inherently multi-task by design (a periodic tick driver
// task, the stratum/accept path calling tm_pool_policy_record_accepted_
// share(), and a config/HTTP path calling the runtime setters, all
// concurrently). Every access to s_state (tick, record_accepted_share,
// init, active_idx, and all 3 setters) holds s_lock for its duration. The
// internal s_*_locked() helpers assume the caller already holds s_lock --
// they never take or release it themselves. s_load_cfg() is the one
// exception: it touches only its `out` parameter, never s_state, so it is
// safe to call without the lock held (kept unsuffixed for that reason,
// mirroring tm_pool_stats's s_load_from_storage()).
//
// No internal timer: tm_pool_policy_tick() is externally driven, the same
// way tm_pool_client_ops_t.service() and stratum_fsm_service() are -- its
// caller supplies both a live tm_pool_client_ops_t/ctx (unknown at init
// time; composition binding this to an actual pool-client instance is a
// later PR) and the injected clock. There is deliberately no ESP-gated
// bb_timer object here (unlike tm_pool_stats's self-armed periodic flush
// timer): a periodic bb_timer callback firing tick() itself would need the
// very ops/ctx pair this component cannot own or default-construct, so the
// cadence stays entirely the composer's responsibility.

#include "tm_pool_policy.h"
#include "tm_pool_cfg.h" // TM_POOL_MAX, tm_pool_config_is_configured()

#include "bb_config.h"
#include "bb_lock.h"
#include "bb_lock_once.h"

#include <string.h>

#define TM_POOL_POLICY_NVS_NS "tm_pool"

// -----------------------------------------------------------------------
// Persisted field table
// -----------------------------------------------------------------------

static const bb_config_field_t s_f_mode = {
    .id          = "pool.sel_mode",
    .type        = BB_CONFIG_U8,
    .addr        = { .backend = "nvs", .ns_or_dir = TM_POOL_POLICY_NVS_NS, .key = "sel_mode" },
    .has_default = true,
    .def         = { .u8 = (uint8_t)TM_POOL_SEL_MANUAL },
};

static const bb_config_field_t s_f_manual_idx = {
    .id          = "pool.sel_manual_idx",
    .type        = BB_CONFIG_U8,
    .addr        = { .backend = "nvs", .ns_or_dir = TM_POOL_POLICY_NVS_NS, .key = "sel_manual_idx" },
    .has_default = true,
    .def         = { .u8 = 0 },
};

static const bb_config_field_t s_f_rotate_en = {
    .id          = "pool.rotate_en",
    .type        = BB_CONFIG_BOOL,
    .addr        = { .backend = "nvs", .ns_or_dir = TM_POOL_POLICY_NVS_NS, .key = "rotate_en" },
    .has_default = true,
    .def         = { .b = false },
};

static const bb_config_field_t s_f_rotate_hrs = {
    .id          = "pool.rotate_hrs",
    .type        = BB_CONFIG_U32,
    .addr        = { .backend = "nvs", .ns_or_dir = TM_POOL_POLICY_NVS_NS, .key = "rotate_hrs" },
    .has_default = true,
    .def         = { .u32 = 0 },
};

static const bb_config_field_t s_f_rotate_shares = {
    .id          = "pool.rotate_shares",
    .type        = BB_CONFIG_U32,
    .addr        = { .backend = "nvs", .ns_or_dir = TM_POOL_POLICY_NVS_NS, .key = "rotate_shares" },
    .has_default = true,
    .def         = { .u32 = 0 },
};

static const bb_config_field_t s_f_rotate_order = {
    .id          = "pool.rotate_order",
    .type        = BB_CONFIG_U8,
    .addr        = { .backend = "nvs", .ns_or_dir = TM_POOL_POLICY_NVS_NS, .key = "rotate_order" },
    .has_default = true,
    .def         = { .u8 = 0 },
};

// -----------------------------------------------------------------------
// RAM state + internal lock. See the file-level doc comment above for the
// concurrency contract.
// -----------------------------------------------------------------------

typedef struct {
    tm_pool_policy_cfg_t cfg;

    uint8_t active_idx;

    // The active_idx value this component last requested a reconnect
    // against, regardless of which mode produced it -- updated by EVERY
    // branch that calls ops->request_reconnect() (MANUAL on a change,
    // FAILOVER/AUTO_ROTATE on every advance). This is what makes a MANUAL
    // switch mid-run correctly detect "the previously-active slot (however
    // FAILOVER/AUTO_ROTATE got there) differs from the new manual_idx" --
    // comparing against a MANUAL-only-updated tracker would miss a change
    // that happened entirely under FAILOVER/AUTO_ROTATE before the switch.
    uint8_t synced_idx;

    uint8_t  fail_count;
    uint32_t rotate_accum_shares;
    uint32_t rotate_window_start_ms;

    // Set by tm_pool_policy_set_mode() when it switches INTO AUTO_ROTATE
    // from a DIFFERENT prior mode; consumed (and cleared) by the first
    // AUTO_ROTATE tick that follows, which primes rotate_window_start_ms/
    // rotate_accum_shares to that tick's now_ms/0 before evaluating any
    // trigger. See tm_pool_policy_tick()'s AUTO_ROTATE doc comment for the
    // hazard this closes (a long prior uptime or cross-mode share accrual
    // otherwise firing an immediate rotation on mode entry).
    bool rotate_needs_prime;
} tm_pool_policy_state_t;

static bb_once_t s_lock_once = BB_ONCE_INIT;
static bb_lock_t s_lock;

static tm_pool_policy_state_t s_state;

// -----------------------------------------------------------------------
// Storage helpers.
// -----------------------------------------------------------------------

// No shared state touched -- safe to call without s_lock held (see the
// file-level doc comment).
static bb_err_t s_load_cfg(tm_pool_policy_cfg_t *out)
{
    memset(out, 0, sizeof(*out));

    uint8_t  mode_raw = (uint8_t)TM_POOL_SEL_MANUAL;
    bb_err_t err       = bb_config_get_u8(&s_f_mode, &mode_raw);
    if (err != BB_OK) {
        return err;
    }
    out->mode = (tm_pool_sel_mode_t)mode_raw;

    err = bb_config_get_u8(&s_f_manual_idx, &out->manual_idx);
    if (err != BB_OK) {
        return err;
    }

    err = bb_config_get_bool(&s_f_rotate_en, &out->rotate_enabled);
    if (err != BB_OK) {
        return err;
    }

    err = bb_config_get_u32(&s_f_rotate_hrs, &out->rotate_interval_hours);
    if (err != BB_OK) {
        return err;
    }

    err = bb_config_get_u32(&s_f_rotate_shares, &out->rotate_share_threshold);
    if (err != BB_OK) {
        return err;
    }

    err = bb_config_get_u8(&s_f_rotate_order, &out->rotate_order);
    if (err != BB_OK) {
        return err;
    }

    return BB_OK;
}

// -----------------------------------------------------------------------
// Pure decision helpers. Caller must hold s_lock (they read/return based on
// s_state/tm_pool_config, though none mutate s_state directly).
// -----------------------------------------------------------------------

// Scans forward from (from_idx + 1), wrapping through every slot including
// from_idx itself, and returns the first CONFIGURED slot found. If from_idx
// is the only configured slot (or nothing at all is configured), returns
// from_idx unchanged -- "nowhere else to advance to" is represented as a
// no-op advance, not an error.
static uint8_t s_next_configured_locked(uint8_t from_idx)
{
    for (uint8_t step = 1; step <= TM_POOL_MAX; step++) {
        uint8_t cand = (uint8_t)((from_idx + step) % TM_POOL_MAX);
        if (tm_pool_config_is_configured(cand)) {
            return cand;
        }
    }
    return from_idx;
}

static uint8_t s_derive_initial_active_idx_locked(void)
{
    if (s_state.cfg.mode == TM_POOL_SEL_MANUAL) {
        return s_state.cfg.manual_idx;
    }
    for (uint8_t i = 0; i < TM_POOL_MAX; i++) {
        if (tm_pool_config_is_configured(i)) {
            return i;
        }
    }
    return 0; // nothing configured yet -- safe fallback
}

// -----------------------------------------------------------------------
// Public API -- each entry point acquires s_lock for its full duration.
// -----------------------------------------------------------------------

bb_err_t tm_pool_policy_init(void)
{
    bb_lock_config_t lcfg = { .name = "tm_pool_policy", .category = "pool" };
    bb_err_t         err  = bb_lock_once_ensure(&s_lock_once, &lcfg, &s_lock);
    if (err != BB_OK) {
        return err;
    }

    bb_lock_lock(&s_lock);
    err = s_load_cfg(&s_state.cfg);
    if (err == BB_OK) {
        s_state.fail_count             = 0;
        s_state.rotate_accum_shares    = 0;
        s_state.rotate_window_start_ms = 0;
        s_state.active_idx             = s_derive_initial_active_idx_locked();
        s_state.synced_idx             = s_state.active_idx;
        s_state.rotate_needs_prime     = false; // fresh RAM state already IS the primed baseline
    }
    bb_lock_unlock(&s_lock);
    return err;
}

void tm_pool_policy_tick(const tm_pool_client_ops_t *ops, void *client_ctx, uint32_t now_ms)
{
    if (ops == NULL) {
        return;
    }

    bb_lock_lock(&s_lock);
    switch (s_state.cfg.mode) {
    case TM_POOL_SEL_MANUAL:
        s_state.active_idx = s_state.cfg.manual_idx;
        if (s_state.active_idx != s_state.synced_idx) {
            s_state.synced_idx = s_state.active_idx;
            if (ops->request_reconnect) {
                ops->request_reconnect(client_ctx);
            }
        }
        break;

    case TM_POOL_SEL_FAILOVER:
        if (ops->consumed_read_failure && ops->consumed_read_failure(client_ctx)) {
            s_state.fail_count++;
            if (s_state.fail_count >= TM_POOL_FAILOVER_THRESHOLD) {
                s_state.active_idx = s_next_configured_locked(s_state.active_idx);
                s_state.synced_idx = s_state.active_idx;
                s_state.fail_count = 0;
                if (ops->request_reconnect) {
                    ops->request_reconnect(client_ctx);
                }
            }
        }
        break;

    case TM_POOL_SEL_AUTO_ROTATE:
        if (s_state.rotate_needs_prime) {
            s_state.rotate_window_start_ms = now_ms;
            s_state.rotate_accum_shares    = 0;
            s_state.rotate_needs_prime     = false;
        }
        if (s_state.cfg.rotate_enabled) {
            bool time_trigger = false;
            if (s_state.cfg.rotate_interval_hours > 0) {
                uint64_t interval_ms = (uint64_t)s_state.cfg.rotate_interval_hours * 3600000ULL;
                uint64_t elapsed_ms  = (uint64_t)(uint32_t)(now_ms - s_state.rotate_window_start_ms);
                time_trigger         = elapsed_ms >= interval_ms;
            }
            bool share_trigger = s_state.cfg.rotate_share_threshold > 0 &&
                                  s_state.rotate_accum_shares >= s_state.cfg.rotate_share_threshold;

            if (time_trigger || share_trigger) {
                s_state.active_idx             = s_next_configured_locked(s_state.active_idx);
                s_state.synced_idx             = s_state.active_idx;
                s_state.rotate_accum_shares    = 0;
                s_state.rotate_window_start_ms = now_ms;
                if (ops->request_reconnect) {
                    ops->request_reconnect(client_ctx);
                }
            }
        }
        break;
    }
    bb_lock_unlock(&s_lock);
}

void tm_pool_policy_record_accepted_share(void)
{
    bb_lock_lock(&s_lock);
    s_state.rotate_accum_shares++;
    bb_lock_unlock(&s_lock);
}

uint8_t tm_pool_policy_active_idx(void)
{
    bb_lock_lock(&s_lock);
    uint8_t idx = s_state.active_idx;
    bb_lock_unlock(&s_lock);
    return idx;
}

bb_err_t tm_pool_policy_set_mode(tm_pool_sel_mode_t mode)
{
    bb_lock_lock(&s_lock);
    bb_err_t err = bb_config_set_u8(&s_f_mode, (uint8_t)mode);
    if (err == BB_OK) {
        bool entering_auto_rotate = mode == TM_POOL_SEL_AUTO_ROTATE && s_state.cfg.mode != TM_POOL_SEL_AUTO_ROTATE;
        s_state.cfg.mode          = mode;
        if (entering_auto_rotate) {
            s_state.rotate_needs_prime = true;
        }
    }
    bb_lock_unlock(&s_lock);
    return err;
}

bb_err_t tm_pool_policy_set_manual_idx(uint8_t idx)
{
    if (idx >= TM_POOL_MAX) {
        return BB_ERR_INVALID_ARG;
    }

    bb_lock_lock(&s_lock);
    bb_err_t err = bb_config_set_u8(&s_f_manual_idx, idx);
    if (err == BB_OK) {
        s_state.cfg.manual_idx = idx;
    }
    bb_lock_unlock(&s_lock);
    return err;
}

bb_err_t tm_pool_policy_set_rotate_cfg(bool enabled, uint32_t interval_hours,
                                        uint32_t share_threshold, uint8_t order)
{
    bb_lock_lock(&s_lock);

    bb_err_t err = bb_config_set_bool(&s_f_rotate_en, enabled);
    if (err != BB_OK) {
        bb_lock_unlock(&s_lock);
        return err;
    }
    s_state.cfg.rotate_enabled = enabled;

    err = bb_config_set_u32(&s_f_rotate_hrs, interval_hours);
    if (err != BB_OK) {
        bb_lock_unlock(&s_lock);
        return err;
    }
    s_state.cfg.rotate_interval_hours = interval_hours;

    err = bb_config_set_u32(&s_f_rotate_shares, share_threshold);
    if (err != BB_OK) {
        bb_lock_unlock(&s_lock);
        return err;
    }
    s_state.cfg.rotate_share_threshold = share_threshold;

    err = bb_config_set_u8(&s_f_rotate_order, order);
    if (err == BB_OK) {
        s_state.cfg.rotate_order = order;
    }

    bb_lock_unlock(&s_lock);
    return err;
}
