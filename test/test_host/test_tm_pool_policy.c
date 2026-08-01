// Host tests for the tm_pool_policy selection engine (TA-572/TA-203):
// MANUAL/FAILOVER/AUTO_ROTATE tick transitions, boot derivation, runtime
// setters, and the internal bb_lock discipline (exercised indirectly --
// every entry point below is single-exit/lock-guarded by construction; a
// deadlock or unbalanced lock would hang or assert these tests outright).
//
// Fake tm_pool_client_ops_t vtable -- same pattern test_stratum_pool_client.c
// uses for its own fakes, sized down to only what tm_pool_policy_tick()
// actually drives (consumed_read_failure, request_reconnect). init/service/
// is_connected/teardown are never called by this component and are left
// NULL in s_fake_ops -- tm_pool_policy_tick() never touches them.
#include "unity.h"
#include "tm_pool_policy.h"
#include "tm_pool_cfg.h"
#include "bb_storage.h"
#include "fake_nvs_backend.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Fake ops vtable
// ---------------------------------------------------------------------------

typedef struct {
    // consumed_read_failure() pops one entry per call, in order; once
    // exhausted, returns false.
    bool    failure_seq[16];
    int     failure_seq_len;
    int     failure_seq_pos;

    int     reconnect_calls;
    int     consumed_read_failure_calls;
} fake_ops_state_t;

static fake_ops_state_t s_fake;

static bool fake_consumed_read_failure(void *ctx)
{
    (void)ctx;
    s_fake.consumed_read_failure_calls++;
    if (s_fake.failure_seq_pos >= s_fake.failure_seq_len) {
        return false;
    }
    return s_fake.failure_seq[s_fake.failure_seq_pos++];
}

static bool fake_is_connected(void *ctx)
{
    (void)ctx;
    return false;
}

static void fake_request_reconnect(void *ctx)
{
    (void)ctx;
    s_fake.reconnect_calls++;
}

static const tm_pool_client_ops_t s_fake_ops = {
    .init                  = NULL,
    .service               = NULL,
    .consumed_read_failure = fake_consumed_read_failure,
    .is_connected          = fake_is_connected,
    .request_reconnect     = fake_request_reconnect,
    .teardown              = NULL,
};

static void reset_fake_ops(void)
{
    memset(&s_fake, 0, sizeof(s_fake));
}

static void queue_failures(int n)
{
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_LESS_THAN(16, s_fake.failure_seq_len);
        s_fake.failure_seq[s_fake.failure_seq_len++] = true;
    }
}

// ---------------------------------------------------------------------------
// Config/storage plumbing -- same fake "nvs" backend tm_pool_config/
// tm_pool_stats register, since tm_pool_policy's field table also targets
// backend="nvs".
// ---------------------------------------------------------------------------

static void reset_all(void)
{
    bb_storage_test_reset();
    fake_nvs_reset();
    bb_storage_register_backend("nvs", &s_fake_nvs_vtable, NULL);
    reset_fake_ops();
}

static void configure_pool(uint8_t idx, const char *host)
{
    tm_pool_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.host, host, sizeof(cfg.host) - 1);
    cfg.port = 3333;
    strncpy(cfg.wallet, "wallet-addr", sizeof(cfg.wallet) - 1);
    strncpy(cfg.worker, "worker1", sizeof(cfg.worker) - 1);
    strncpy(cfg.pass, "x", sizeof(cfg.pass) - 1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(idx, &cfg));
}

/* ---------------------------------------------------------------------------
 * init / boot derivation
 * ---------------------------------------------------------------------------*/

void test_tm_pool_policy_init_defaults_to_manual_slot0(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
}

void test_tm_pool_policy_init_manual_mode_derives_manual_idx(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_mode(TM_POOL_SEL_MANUAL));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_manual_idx(2));

    // Re-init (simulates a reboot with the persisted mode/manual_idx already
    // in storage) -- active_idx must derive from the persisted manual_idx,
    // not carry over any prior RAM state.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL_UINT8(2, tm_pool_policy_active_idx());
}

void test_tm_pool_policy_init_failover_mode_derives_first_configured_slot(void)
{
    reset_all();
    configure_pool(1, "pool1.example.com");
    configure_pool(2, "pool2.example.com");

    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_mode(TM_POOL_SEL_FAILOVER));

    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init()); // reboot
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
}

void test_tm_pool_policy_init_auto_rotate_mode_derives_first_configured_slot(void)
{
    reset_all();
    configure_pool(2, "pool2.example.com");

    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_mode(TM_POOL_SEL_AUTO_ROTATE));

    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init()); // reboot
    TEST_ASSERT_EQUAL_UINT8(2, tm_pool_policy_active_idx());
}

void test_tm_pool_policy_init_runtime_state_starts_fresh_not_persisted(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_mode(TM_POOL_SEL_FAILOVER));

    // Drive fail_count to 2 (one below threshold) -- must NOT survive a
    // re-init: after re-init, 2 MORE failures (not just 1) are required to
    // trigger the advance.
    queue_failures(2);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1000);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1001);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init()); // reboot -- fail_count resets to 0
    reset_fake_ops();
    queue_failures(2);
    tm_pool_policy_tick(&s_fake_ops, NULL, 2000);
    tm_pool_policy_tick(&s_fake_ops, NULL, 2001);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx()); // still below threshold (2 < 3)
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);
}

/* ---------------------------------------------------------------------------
 * MANUAL mode
 * ---------------------------------------------------------------------------*/

void test_tm_pool_policy_manual_tracks_manual_idx(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_manual_idx(1));

    tm_pool_policy_tick(&s_fake_ops, NULL, 1000);
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
}

void test_tm_pool_policy_manual_stable_idx_triggers_no_reconnect(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_manual_idx(1));

    tm_pool_policy_tick(&s_fake_ops, NULL, 1000); // idx changes 0 -> 1, one reconnect
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);

    tm_pool_policy_tick(&s_fake_ops, NULL, 1001);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1002);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1003);
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls); // stable idx -- no further reconnects
}

void test_tm_pool_policy_manual_idx_change_triggers_exactly_one_reconnect(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());

    tm_pool_policy_tick(&s_fake_ops, NULL, 1000); // manual_idx=0, active_idx already 0
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_manual_idx(2));
    tm_pool_policy_tick(&s_fake_ops, NULL, 1001);
    TEST_ASSERT_EQUAL_UINT8(2, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);

    tm_pool_policy_tick(&s_fake_ops, NULL, 1002);
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls); // stable again
}

void test_tm_pool_policy_manual_short_circuits_failover_and_rotate_bookkeeping(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    // mode defaults to MANUAL -- consumed_read_failure() must never even be
    // called while in MANUAL mode.
    queue_failures(5);
    for (int i = 0; i < 5; i++) {
        tm_pool_policy_tick(&s_fake_ops, NULL, (uint32_t)(1000 + i));
    }
    TEST_ASSERT_EQUAL_INT(0, s_fake.consumed_read_failure_calls);
}

/* ---------------------------------------------------------------------------
 * FAILOVER mode
 * ---------------------------------------------------------------------------*/

static void set_failover(void)
{
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_mode(TM_POOL_SEL_FAILOVER));
}

void test_tm_pool_policy_failover_below_threshold_does_not_advance(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_failover();

    queue_failures(2);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1000);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1001);

    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);
}

void test_tm_pool_policy_failover_three_consecutive_failures_advances_resets_reconnects(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_failover();

    queue_failures(3);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1000);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1001);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1002);

    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);

    // fail_count reset -- 2 MORE failures alone must not re-trigger.
    reset_fake_ops();
    queue_failures(2);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1003);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1004);
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);
}

void test_tm_pool_policy_failover_skips_unconfigured_slot_and_wraps(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    // slot 1 intentionally left unconfigured.
    configure_pool(2, "pool2.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_failover();

    // 0 -> (skip 1) -> 2
    queue_failures(3);
    for (int i = 0; i < 3; i++) {
        tm_pool_policy_tick(&s_fake_ops, NULL, (uint32_t)(1000 + i));
    }
    TEST_ASSERT_EQUAL_UINT8(2, tm_pool_policy_active_idx());

    // 2 -> (wrap, skip 1) -> 0
    reset_fake_ops();
    queue_failures(3);
    for (int i = 0; i < 3; i++) {
        tm_pool_policy_tick(&s_fake_ops, NULL, (uint32_t)(2000 + i));
    }
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
}

void test_tm_pool_policy_failover_no_auto_fail_back(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_failover();

    queue_failures(3);
    for (int i = 0; i < 3; i++) {
        tm_pool_policy_tick(&s_fake_ops, NULL, (uint32_t)(1000 + i));
    }
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());

    // Slot 1 (now active) never fails again -- there is no path that would
    // ever move the engine back to slot 0 on its own.
    reset_fake_ops();
    for (uint32_t t = 2000; t < 2010; t++) {
        tm_pool_policy_tick(&s_fake_ops, NULL, t);
    }
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
}

void test_tm_pool_policy_failover_only_one_configured_pool_reconnects_in_place(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_failover();

    queue_failures(3);
    for (int i = 0; i < 3; i++) {
        tm_pool_policy_tick(&s_fake_ops, NULL, (uint32_t)(1000 + i));
    }

    // Nowhere to advance to -- active_idx is unchanged, but reaching
    // threshold still requests one reconnect (see tm_pool_policy.h's
    // FAILOVER doc comment for the rationale).
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);

    // fail_count still reset -- 2 more failures alone don't re-trigger.
    reset_fake_ops();
    queue_failures(2);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1003);
    tm_pool_policy_tick(&s_fake_ops, NULL, 1004);
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);
}

/* ---------------------------------------------------------------------------
 * AUTO_ROTATE mode
 * ---------------------------------------------------------------------------*/

static void set_auto_rotate(bool enabled, uint32_t interval_hours, uint32_t share_threshold)
{
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_mode(TM_POOL_SEL_AUTO_ROTATE));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_rotate_cfg(enabled, interval_hours, share_threshold, 0));
}

void test_tm_pool_policy_auto_rotate_time_trigger_fires_at_interval(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_auto_rotate(true, 1 /* hour */, 0 /* share trigger disabled */);

    // The first AUTO_ROTATE tick after the mode switch primes the window to
    // ITS OWN now_ms (see test_tm_pool_policy_auto_rotate_primes_window_on_
    // mode_entry_time_trigger below for the hazard this closes) -- consume
    // that priming tick at t=0 so the interval below is measured from a
    // known baseline.
    tm_pool_policy_tick(&s_fake_ops, NULL, 0);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    uint32_t one_hour_ms = 3600000;
    tm_pool_policy_tick(&s_fake_ops, NULL, one_hour_ms - 1);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    tm_pool_policy_tick(&s_fake_ops, NULL, one_hour_ms);
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);
}

void test_tm_pool_policy_auto_rotate_share_trigger_fires_at_threshold(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_auto_rotate(true, 0 /* time trigger disabled */, 3 /* shares */);

    // Consume the mode-entry priming tick first (it resets the accumulator
    // regardless of what's been recorded before this component's FIRST
    // AUTO_ROTATE tick -- see the priming tests below).
    tm_pool_policy_tick(&s_fake_ops, NULL, 999);
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    tm_pool_policy_record_accepted_share();
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_tick(&s_fake_ops, NULL, 1000);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    tm_pool_policy_record_accepted_share(); // 3rd share -- hits threshold
    tm_pool_policy_tick(&s_fake_ops, NULL, 1001);
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);
}

void test_tm_pool_policy_auto_rotate_either_trigger_resets_both_accumulators(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    configure_pool(2, "pool2.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_auto_rotate(true, 1 /* hour */, 5 /* shares */);

    // Consume the mode-entry priming tick at t=0 -- window_start=0 becomes
    // the known baseline the "one hour" below is measured from.
    tm_pool_policy_tick(&s_fake_ops, NULL, 0);
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    // Accumulate 4 shares (below the 5 threshold), then fire via the TIME
    // trigger -- both accumulators (shares AND the time window) must reset,
    // so the very next tick (still short of a full new hour, and still
    // short of 5 fresh shares) does NOT fire again.
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_record_accepted_share();

    uint32_t one_hour_ms = 3600000;
    tm_pool_policy_tick(&s_fake_ops, NULL, one_hour_ms);
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);

    tm_pool_policy_tick(&s_fake_ops, NULL, one_hour_ms + 1);
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx()); // did not re-fire
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);
}

void test_tm_pool_policy_auto_rotate_disabled_fires_neither_trigger(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_auto_rotate(false, 1, 1);

    tm_pool_policy_record_accepted_share();
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_tick(&s_fake_ops, NULL, 3600000 * 10);

    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);
}

void test_tm_pool_policy_auto_rotate_both_thresholds_zero_never_fires(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_auto_rotate(true, 0, 0);

    for (int i = 0; i < 100; i++) {
        tm_pool_policy_record_accepted_share();
    }
    tm_pool_policy_tick(&s_fake_ops, NULL, 3600000 * 100);

    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);
}

void test_tm_pool_policy_auto_rotate_round_robins_across_configured_slots(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    // slot 1 intentionally unconfigured.
    configure_pool(2, "pool2.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_auto_rotate(true, 0, 1 /* share */);

    // Consume the mode-entry priming tick first.
    tm_pool_policy_tick(&s_fake_ops, NULL, 999);
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    tm_pool_policy_record_accepted_share();
    tm_pool_policy_tick(&s_fake_ops, NULL, 1000); // 0 -> (skip 1) -> 2
    TEST_ASSERT_EQUAL_UINT8(2, tm_pool_policy_active_idx());

    tm_pool_policy_record_accepted_share();
    tm_pool_policy_tick(&s_fake_ops, NULL, 1001); // 2 -> (wrap, skip 1) -> 0
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
}

/* ---------------------------------------------------------------------------
 * AUTO_ROTATE mode-entry priming (firmware review MEDIUM finding): a device
 * that spent a long time in a DIFFERENT mode before switching to
 * AUTO_ROTATE must not see that prior uptime/share-accrual instantly
 * satisfy the very first trigger check.
 * ---------------------------------------------------------------------------*/

void test_tm_pool_policy_auto_rotate_primes_window_on_mode_entry_time_trigger(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init()); // mode defaults to MANUAL

    // Simulate a long uptime in MANUAL before switching modes.
    uint32_t long_uptime_ms = 3600000UL * 10; // 10 hours
    tm_pool_policy_tick(&s_fake_ops, NULL, long_uptime_ms);

    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_mode(TM_POOL_SEL_AUTO_ROTATE));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_rotate_cfg(true, 1 /* hour */, 0, 0));

    // The FIRST AUTO_ROTATE tick, even at the SAME now_ms as the long prior
    // uptime, must NOT rotate -- without priming, now_ms - window_start(0)
    // would already exceed the 1-hour interval by 9 hours.
    tm_pool_policy_tick(&s_fake_ops, NULL, long_uptime_ms);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    // Not yet a full hour since the switch-in (priming) tick.
    tm_pool_policy_tick(&s_fake_ops, NULL, long_uptime_ms + 3600000UL - 1);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    // Exactly one hour after the switch-in tick -- rotates on schedule
    // measured from mode ENTRY, not from boot.
    tm_pool_policy_tick(&s_fake_ops, NULL, long_uptime_ms + 3600000UL);
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);
}

void test_tm_pool_policy_auto_rotate_primes_share_accum_on_mode_entry(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());

    // Pre-accumulate shares while in a DIFFERENT (default MANUAL) mode.
    for (int i = 0; i < 10; i++) {
        tm_pool_policy_record_accepted_share();
    }

    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_mode(TM_POOL_SEL_AUTO_ROTATE));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_rotate_cfg(true, 0, 3 /* shares */, 0));

    // The first AUTO_ROTATE tick primes (resets accum to 0) -- must NOT
    // fire despite the 10 shares accumulated before the switch.
    tm_pool_policy_tick(&s_fake_ops, NULL, 1000);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    // 2 fresh shares -- still below the 3 threshold.
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_tick(&s_fake_ops, NULL, 1001);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);

    // 3rd fresh share hits the threshold -- fires.
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_tick(&s_fake_ops, NULL, 1002);
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);
}

/* ---------------------------------------------------------------------------
 * Mode switch mid-run
 * ---------------------------------------------------------------------------*/

void test_tm_pool_policy_failover_to_manual_switch_overrides_next_tick(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    set_failover();

    queue_failures(3);
    for (int i = 0; i < 3; i++) {
        tm_pool_policy_tick(&s_fake_ops, NULL, (uint32_t)(1000 + i));
    }
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());

    reset_fake_ops();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_manual_idx(0));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_mode(TM_POOL_SEL_MANUAL));

    // Still FAILOVER's active_idx (1) until the NEXT tick.
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());

    tm_pool_policy_tick(&s_fake_ops, NULL, 2000);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);
    TEST_ASSERT_EQUAL_INT(0, s_fake.consumed_read_failure_calls); // FAILOVER bookkeeping never runs again
}

/* ---------------------------------------------------------------------------
 * Runtime setters
 * ---------------------------------------------------------------------------*/

void test_tm_pool_policy_set_manual_idx_rejects_out_of_range(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_policy_set_manual_idx(TM_POOL_MAX));
}

void test_tm_pool_policy_tick_null_ops_is_a_noop(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    tm_pool_policy_tick(NULL, NULL, 1000); // must not crash
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());
}

/* ---------------------------------------------------------------------------
 * Failure injection -- fake_nvs_backend_fail_key/fail_set_key hooks (same
 * pattern test_tm_pool_stats.c/test_tm_pool_config.c use).
 * ---------------------------------------------------------------------------*/

void test_tm_pool_policy_init_propagates_first_genuine_read_failure(void)
{
    reset_all();
    fake_nvs_backend_fail_key("sel_mode"); // first field s_load_cfg() reads
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_policy_init());
}

void test_tm_pool_policy_set_mode_propagates_write_failure_and_leaves_ram_unchanged(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init()); // mode=MANUAL (default)

    fake_nvs_backend_fail_set_key("sel_mode");
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_policy_set_mode(TM_POOL_SEL_FAILOVER));

    // RAM mode must still be MANUAL -- a tick must never touch FAILOVER's
    // consumed_read_failure() bookkeeping.
    queue_failures(3);
    for (int i = 0; i < 3; i++) {
        tm_pool_policy_tick(&s_fake_ops, NULL, (uint32_t)(1000 + i));
    }
    TEST_ASSERT_EQUAL_INT(0, s_fake.consumed_read_failure_calls);
}

void test_tm_pool_policy_set_manual_idx_propagates_write_failure_and_leaves_ram_unchanged(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init()); // manual_idx=0 (default)

    fake_nvs_backend_fail_set_key("sel_manual_idx");
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_policy_set_manual_idx(2));

    tm_pool_policy_tick(&s_fake_ops, NULL, 1000);
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx()); // unchanged -- still 0, not 2
    TEST_ASSERT_EQUAL_INT(0, s_fake.reconnect_calls);        // 0 == 0 was already synced -- no reconnect
}

// Per set_rotate_cfg()'s documented partial-write contract: a field that DID
// persist successfully before a LATER field fails is also applied to RAM,
// even though the overall call returns an error.
void test_tm_pool_policy_set_rotate_cfg_partial_write_applies_ram_up_to_failing_field(void)
{
    reset_all();
    configure_pool(0, "pool0.example.com");
    configure_pool(1, "pool1.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_mode(TM_POOL_SEL_AUTO_ROTATE));

    // Baseline: rotate disabled, share threshold already persisted at 3.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_set_rotate_cfg(false, 0, 3, 0));
    tm_pool_policy_tick(&s_fake_ops, NULL, 0); // consume mode-entry priming

    // Flip enabled=true and attempt to change interval_hours to 5 AND
    // share_threshold to 999, but inject a write failure on the SECOND
    // field (rotate_hrs) -- the call must stop there: `enabled` (1st field)
    // IS persisted+applied, interval_hours/share_threshold (2nd/3rd,
    // reached-or-not) are NOT.
    fake_nvs_backend_fail_set_key("rotate_hrs");
    bb_err_t err = tm_pool_policy_set_rotate_cfg(true, 5, 999, 0);
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, err);

    // enabled=true DID take effect -- the baseline share_threshold=3 (never
    // reached/overwritten by the failed call) still governs: exactly 3
    // shares fires.
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_record_accepted_share();
    tm_pool_policy_tick(&s_fake_ops, NULL, 1);
    TEST_ASSERT_EQUAL_UINT8(1, tm_pool_policy_active_idx());
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);
}

/* ---------------------------------------------------------------------------
 * Zero configured pools -- a fresh device with no pool set up at all.
 * ---------------------------------------------------------------------------*/

void test_tm_pool_policy_zero_configured_pools_failover_stays_at_fallback_and_still_reconnects(void)
{
    reset_all(); // no configure_pool() calls -- nothing configured
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx()); // init-derived fallback

    set_failover();
    queue_failures(3);
    for (int i = 0; i < 3; i++) {
        tm_pool_policy_tick(&s_fake_ops, NULL, (uint32_t)(1000 + i));
    }

    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx()); // nowhere to advance to
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);        // threshold event still reconnects
}

void test_tm_pool_policy_zero_configured_pools_auto_rotate_stays_at_fallback_and_still_reconnects(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());
    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx());

    set_auto_rotate(true, 0, 1 /* share */);
    tm_pool_policy_tick(&s_fake_ops, NULL, 999); // consume mode-entry priming

    tm_pool_policy_record_accepted_share();
    tm_pool_policy_tick(&s_fake_ops, NULL, 1000);

    TEST_ASSERT_EQUAL_UINT8(0, tm_pool_policy_active_idx()); // nowhere to advance to
    TEST_ASSERT_EQUAL_INT(1, s_fake.reconnect_calls);        // trigger still reconnects
}
