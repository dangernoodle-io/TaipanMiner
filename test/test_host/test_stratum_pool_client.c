// Host tests for the tm_pool_client seam (TA-571): the V1 (line-protocol
// Stratum) adapter's cfg mapping + vtable forwarding, the one-shot
// consumed_read_failure flag's fire/no-fire conditions (one case per hard-
// failure event), and the on-accepted-share hook (including its behavior
// under reqid-table churn, the desync class of bug firmware review finding
// #2 fixed).
//
// tm_stratum must NEVER reference tm_pool_stats (or any other delivery/
// recording sink) -- these tests exercise the hook purely as a plain
// function pointer + capture struct, exactly what a composition root would
// bind against later.
#include "unity.h"
#include "stratum_pool_client.h"
#include "test_stratum_fakes.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Fake work seam -- kept local to this file (small, only used here).
// ---------------------------------------------------------------------------

typedef struct {
    bool             have_result;
    mining_result_t  queued_result;
} fake_work_t;

static fake_work_t s_fw;

static bool fake_publish(void *ctx, const mining_work_t *work)
{
    (void)ctx; (void)work;
    return true;
}

static bool fake_drain(void *ctx, mining_result_t *out)
{
    (void)ctx;
    if (s_fw.have_result) {
        *out = s_fw.queued_result;
        s_fw.have_result = false;
        return true;
    }
    return false;
}

static stratum_transport_ops_t s_tops;
static tm_pool_work_ops_t      s_wops;

static void reset_fakes(void)
{
    fake_transport_reset();
    memset(&s_fw, 0, sizeof(s_fw));
}

// Builds ctx->fsm directly via stratum_fsm_init() (bypassing the adapter's
// own init(), which binds its own internal production transport that never
// connects on host) so these tests can drive full sessions with the same
// fake-transport pattern test_stratum_fsm.c uses, while still calling
// through the REAL tm_pool_client_ops_t vtable for every other operation.
// ctx->transport is left zeroed/unused (only ctx->fsm is exercised this
// way) -- see test_stratum_pool_client_init_maps_cfg_fields for the ONE
// test that drives the real init() path instead.
static void make_ctx(stratum_pool_client_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    fake_transport_bind(&s_tops);

    s_wops.ctx = NULL;
    s_wops.publish = fake_publish;
    s_wops.drain = fake_drain;
    s_wops.reset = NULL;

    stratum_fsm_cfg_t cfg = {
        .host = "pool.example.com",
        .port = 3333,
        .wallet = "wallet-addr",
        .worker = "worker1",
        .pass = "x",
        .extranonce_subscribe = false,
        .transport = &s_tops,
        .work = &s_wops,
    };
    stratum_fsm_init(&ctx->fsm, &cfg);
}

static uint32_t drive_to_running(stratum_pool_client_ctx_t *ctx, uint32_t start_now)
{
    uint32_t now = start_now;
    stratum_fsm_service(&ctx->fsm, now);
    now += 1;
    stratum_fsm_service(&ctx->fsm, now);  // -> SUBSCRIBE (id=2)

    s_ft.pending_lines[s_ft.pending_count++] =
        "{\"id\":2,\"result\":[[\"mining.set_difficulty\",\"sub-1\"],\"08000002\",4]}";
    now += 1;
    stratum_fsm_service(&ctx->fsm, now);  // -> AUTHORIZE (id=3)

    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":3,\"result\":true}";
    now += 1;
    stratum_fsm_service(&ctx->fsm, now);  // -> RUNNING
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_RUNNING, stratum_fsm_state(&ctx->fsm));
    return now;
}

// ---------------------------------------------------------------------------
// Adapter: tm_pool_cfg_t -> stratum_fsm_cfg_t mapping
// ---------------------------------------------------------------------------

void test_stratum_pool_client_init_maps_cfg_fields(void)
{
    tm_pool_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.host, "pool.example.com", sizeof(cfg.host) - 1);
    cfg.port = 3333;
    strncpy(cfg.wallet, "wallet-addr", sizeof(cfg.wallet) - 1);
    strncpy(cfg.worker, "worker1", sizeof(cfg.worker) - 1);
    strncpy(cfg.pass, "x", sizeof(cfg.pass) - 1);
    cfg.extranonce_subscribe = true;

    tm_pool_work_ops_t work;
    memset(&work, 0, sizeof(work));

    stratum_pool_client_ctx_t ctx;
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();

    TEST_ASSERT_EQUAL(BB_OK, ops->init(&ctx, &cfg, &work));
    TEST_ASSERT_EQUAL_STRING("pool.example.com", ctx.fsm.cfg.host);
    TEST_ASSERT_EQUAL_UINT16(3333, ctx.fsm.cfg.port);
    TEST_ASSERT_EQUAL_STRING("wallet-addr", ctx.fsm.cfg.wallet);
    TEST_ASSERT_EQUAL_STRING("worker1", ctx.fsm.cfg.worker);
    TEST_ASSERT_EQUAL_STRING("x", ctx.fsm.cfg.pass);
    TEST_ASSERT_TRUE(ctx.fsm.cfg.extranonce_subscribe);
    TEST_ASSERT_EQUAL_PTR(&work, ctx.fsm.cfg.work);
    TEST_ASSERT_EQUAL_PTR(&ctx.transport, ctx.fsm.cfg.transport);  // per-instance, not a file-scope singleton
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx.fsm));
}

void test_stratum_pool_client_init_rejects_null_args(void)
{
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();
    tm_pool_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    tm_pool_work_ops_t work;
    memset(&work, 0, sizeof(work));
    stratum_pool_client_ctx_t ctx;

    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, ops->init(NULL, &cfg, &work));
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, ops->init(&ctx, NULL, &work));
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, ops->init(&ctx, &cfg, NULL));
}

// Two independently init'd instances must never share transport state --
// pins the fix for TA-571 finding #4 (previously a file-scope static).
void test_stratum_pool_client_init_gives_each_instance_its_own_transport(void)
{
    tm_pool_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.host, "pool.example.com", sizeof(cfg.host) - 1);
    tm_pool_work_ops_t work;
    memset(&work, 0, sizeof(work));

    stratum_pool_client_ctx_t a, b;
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();
    TEST_ASSERT_EQUAL(BB_OK, ops->init(&a, &cfg, &work));
    TEST_ASSERT_EQUAL(BB_OK, ops->init(&b, &cfg, &work));

    TEST_ASSERT_NOT_EQUAL(a.fsm.cfg.transport, b.fsm.cfg.transport);
}

// ---------------------------------------------------------------------------
// Adapter: vtable forwards to the FSM
// ---------------------------------------------------------------------------

void test_stratum_pool_client_ops_forward_to_fsm(void)
{
    reset_fakes();
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();

    TEST_ASSERT_FALSE(ops->is_connected(&ctx));

    ops->service(&ctx, 1);
    ops->service(&ctx, 2);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_SUBSCRIBE, stratum_fsm_state(&ctx.fsm));

    ops->request_reconnect(&ctx);
    ops->service(&ctx, 3);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx.fsm));
    TEST_ASSERT_EQUAL_INT(1, s_ft.close_calls);

    ops->teardown(&ctx);
    TEST_ASSERT_EQUAL_INT(2, s_ft.close_calls);  // idempotent, callable again
}

// ---------------------------------------------------------------------------
// consumed_read_failure: one-shot, fires on EVERY hard-failure event, never
// on a clean disconnect or a bare (never-established) connect failure.
// ---------------------------------------------------------------------------

void test_stratum_pool_client_consumed_read_failure_one_shot(void)
{
    reset_fakes();
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();

    uint32_t now = drive_to_running(&ctx, 1);

    s_ft.io_error = true;
    now += 1;
    ops->service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx.fsm));

    TEST_ASSERT_TRUE(ops->consumed_read_failure(&ctx));
    TEST_ASSERT_FALSE(ops->consumed_read_failure(&ctx));  // one-shot: false thereafter
}

void test_stratum_pool_client_consumed_read_failure_not_set_on_clean_disconnect(void)
{
    reset_fakes();
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();

    drive_to_running(&ctx, 1);

    ops->request_reconnect(&ctx);
    ops->service(&ctx, 100);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx.fsm));

    TEST_ASSERT_FALSE(ops->consumed_read_failure(&ctx));
}

void test_stratum_pool_client_consumed_read_failure_not_set_on_bare_connect_failure(void)
{
    reset_fakes();
    s_ft.connect_result = false;
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();

    ops->service(&ctx, 1);  // BACKOFF_ELAPSED -> CONNECTING -> TCP_FAILED -> teardown
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx.fsm));

    TEST_ASSERT_FALSE(ops->consumed_read_failure(&ctx));
}

void test_stratum_pool_client_consumed_read_failure_fires_on_handshake_timeout(void)
{
    reset_fakes();
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();

    uint32_t now = 1;
    ops->service(&ctx, now);
    now += 1;
    ops->service(&ctx, now);  // -> SUBSCRIBE, handshake timer armed
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_SUBSCRIBE, stratum_fsm_state(&ctx.fsm));

    now += STRATUM_HANDSHAKE_TIMEOUT_MS + 1;
    ops->service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx.fsm));

    TEST_ASSERT_TRUE(ops->consumed_read_failure(&ctx));
}

void test_stratum_pool_client_consumed_read_failure_fires_on_handshake_rejected(void)
{
    reset_fakes();
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();

    uint32_t now = 1;
    ops->service(&ctx, now);
    now += 1;
    ops->service(&ctx, now);  // -> SUBSCRIBE (id=2)

    s_ft.pending_lines[s_ft.pending_count++] =
        "{\"id\":2,\"result\":[[\"mining.set_difficulty\",\"sub-1\"],\"08000002\",4]}";
    now += 1;
    ops->service(&ctx, now);  // -> AUTHORIZE (id=3)
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_AUTHORIZE, stratum_fsm_state(&ctx.fsm));

    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":3,\"result\":false,\"error\":[24,\"bad worker\",\"\"]}";
    now += 1;
    ops->service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx.fsm));

    TEST_ASSERT_TRUE(ops->consumed_read_failure(&ctx));
}

void test_stratum_pool_client_consumed_read_failure_fires_on_job_drought(void)
{
    reset_fakes();
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();

    uint32_t now = drive_to_running(&ctx, 1);

    now += STRATUM_WATCHDOG_JOB_DROUGHT_MS + 1;
    ops->service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx.fsm));

    TEST_ASSERT_TRUE(ops->consumed_read_failure(&ctx));
}

void test_stratum_pool_client_consumed_read_failure_fires_on_share_drought(void)
{
    reset_fakes();
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();

    uint32_t now = drive_to_running(&ctx, 1);

    now += STRATUM_WATCHDOG_SHARE_DROUGHT_MS + 1;
    ops->service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx.fsm));

    TEST_ASSERT_TRUE(ops->consumed_read_failure(&ctx));
}

// ---------------------------------------------------------------------------
// on-accepted-share hook: fires exactly on ACCEPT, never on REJECT, exact
// record match, safe no-op when unset, slot reclaimed either way, and
// survives reqid-table churn within capacity (TA-571 finding #2 -- the
// desync bug this replaces is unit-tested directly in
// test_stratum_reqid.c; these are the FSM/adapter-level integration cases).
// ---------------------------------------------------------------------------

typedef struct {
    int                        calls;
    stratum_accepted_share_t   last;
} hook_capture_t;

static hook_capture_t s_hook;

static void capture_hook(void *ud, const stratum_accepted_share_t *share)
{
    hook_capture_t *c = (hook_capture_t *)ud;
    c->calls++;
    c->last = *share;
}

static void queue_submit_result(const char *job_id, const char *en2, const char *ntime,
                                const char *nonce, double share_diff)
{
    s_fw.have_result = true;
    memset(&s_fw.queued_result, 0, sizeof(s_fw.queued_result));
    strncpy(s_fw.queued_result.job_id, job_id, sizeof(s_fw.queued_result.job_id) - 1);
    strncpy(s_fw.queued_result.extranonce2_hex, en2, sizeof(s_fw.queued_result.extranonce2_hex) - 1);
    strncpy(s_fw.queued_result.ntime_hex, ntime, sizeof(s_fw.queued_result.ntime_hex) - 1);
    strncpy(s_fw.queued_result.nonce_hex, nonce, sizeof(s_fw.queued_result.nonce_hex) - 1);
    s_fw.queued_result.share_diff = share_diff;
    for (int i = 0; i < 8; i++) s_fw.queued_result.hash_prefix[i] = (uint8_t)(0x10 + i);
}

void test_stratum_pool_client_hook_fires_on_accept_with_exact_record(void)
{
    reset_fakes();
    memset(&s_hook, 0, sizeof(s_hook));
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    tm_stratum_set_accepted_share_hook(&ctx, capture_hook, &s_hook);

    uint32_t now = drive_to_running(&ctx, 1);

    queue_submit_result("job-1", "00000001", "67b1c400", "01020304", 4096.0);
    now += 1;
    stratum_fsm_service(&ctx.fsm, now);  // sends submit, id=4

    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":4,\"result\":true}";
    now += 1;
    stratum_fsm_service(&ctx.fsm, now);

    TEST_ASSERT_EQUAL_UINT32(1, ctx.fsm.accepted);
    TEST_ASSERT_EQUAL_INT(1, s_hook.calls);
    TEST_ASSERT_EQUAL_STRING("job-1", s_hook.last.job_id);
    TEST_ASSERT_EQUAL_STRING("00000001", s_hook.last.extranonce2_hex);
    TEST_ASSERT_EQUAL_UINT8(8, s_hook.last.extranonce2_len);
    TEST_ASSERT_EQUAL_STRING("67b1c400", s_hook.last.ntime_hex);
    TEST_ASSERT_EQUAL_STRING("01020304", s_hook.last.nonce_hex);
    TEST_ASSERT_FALSE(s_hook.last.version_rolled);
    TEST_ASSERT_EQUAL_UINT32(0, s_hook.last.version_bits);
    TEST_ASSERT_EQUAL_DOUBLE(4096.0, s_hook.last.diff);
    uint8_t expected_prefix[8] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_prefix, s_hook.last.hash_prefix, 8);

    // Slot reclaimed: a second (bogus, replayed) response for the same id
    // finds nothing and does not re-fire the hook.
    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":4,\"result\":true}";
    now += 1;
    stratum_fsm_service(&ctx.fsm, now);
    TEST_ASSERT_EQUAL_INT(1, s_hook.calls);
}

void test_stratum_pool_client_hook_does_not_fire_on_reject(void)
{
    reset_fakes();
    memset(&s_hook, 0, sizeof(s_hook));
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    tm_stratum_set_accepted_share_hook(&ctx, capture_hook, &s_hook);

    uint32_t now = drive_to_running(&ctx, 1);

    queue_submit_result("job-2", "00000002", "67b1c401", "05060708", 2048.0);
    now += 1;
    stratum_fsm_service(&ctx.fsm, now);  // sends submit, id=4

    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":4,\"result\":false,\"error\":[23,\"low diff\",\"\"]}";
    now += 1;
    stratum_fsm_service(&ctx.fsm, now);

    TEST_ASSERT_EQUAL_UINT32(1, ctx.fsm.rejected);
    TEST_ASSERT_EQUAL_INT(0, s_hook.calls);

    // Slot reclaimed on reject too: a stale re-delivery of the same id finds
    // nothing.
    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":4,\"result\":true}";
    now += 1;
    stratum_fsm_service(&ctx.fsm, now);
    TEST_ASSERT_EQUAL_INT(0, s_hook.calls);
}

void test_stratum_pool_client_hook_unset_is_safe_no_op(void)
{
    reset_fakes();
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);  // no hook registered

    uint32_t now = drive_to_running(&ctx, 1);

    queue_submit_result("job-3", "00000003", "67b1c402", "090a0b0c", 512.0);
    now += 1;
    stratum_fsm_service(&ctx.fsm, now);

    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":4,\"result\":true}";
    now += 1;
    stratum_fsm_service(&ctx.fsm, now);

    TEST_ASSERT_EQUAL_UINT32(1, ctx.fsm.accepted);  // no crash, counter still updates
}

// TA-571 finding #2's regression test: a submit id registered first, then 7
// MORE (non-submit) ids registered while it's still in flight -- the table
// is now exactly at its 8-slot capacity, so the submit is NOT evicted. With
// the OLD two-table design this scenario was fine too (only genuine
// eviction desynced them); this test instead pins that the MERGED table
// correctly distinguishes a SUBMIT slot's payload from plain (id, kind)
// slots sharing the same storage -- the hook still fires with the exact
// record once the response for id=4 arrives.
void test_stratum_pool_client_hook_fires_when_submit_survives_reqid_churn_under_capacity(void)
{
    reset_fakes();
    memset(&s_hook, 0, sizeof(s_hook));
    stratum_pool_client_ctx_t ctx;
    make_ctx(&ctx);
    tm_stratum_set_accepted_share_hook(&ctx, capture_hook, &s_hook);

    uint32_t now = drive_to_running(&ctx, 1);

    queue_submit_result("job-4", "00000004", "67b1c403", "0a0b0c0d", 8192.0);
    now += 1;
    stratum_fsm_service(&ctx.fsm, now);  // sends submit, id=4 -- occupies 1 of 8 reqid slots

    // Churn: 7 more (non-submit) registrations land while the submit is
    // still awaiting its response -- table now exactly at capacity (8),
    // the submit slot is NOT evicted.
    for (int i = 0; i < STRATUM_REQID_MAX_INFLIGHT - 1; i++) {
        stratum_reqid_register(&ctx.fsm.reqids, 500 + i, STRATUM_REQID_KEEPALIVE);
    }

    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":4,\"result\":true}";
    now += 1;
    stratum_fsm_service(&ctx.fsm, now);

    TEST_ASSERT_EQUAL_UINT32(1, ctx.fsm.accepted);
    TEST_ASSERT_EQUAL_INT(1, s_hook.calls);
    TEST_ASSERT_EQUAL_STRING("job-4", s_hook.last.job_id);
    TEST_ASSERT_EQUAL_STRING("0a0b0c0d", s_hook.last.nonce_hex);
    TEST_ASSERT_EQUAL_DOUBLE(8192.0, s_hook.last.diff);
}
