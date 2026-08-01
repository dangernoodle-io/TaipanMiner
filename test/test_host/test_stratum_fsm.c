// Host tests for the stratum protocol FSM (TA-560): every transition, the
// single-funnel teardown invariant (TA-530), backoff cap + reset-on-
// handshake (TA-232), the drought/keepalive timers, and keepalive-id
// tracking. The TA-233/TA-494 "stratum never blocks the caller" isolation
// invariant has its own dedicated test file (test_stratum_isolation.c).
//
// The work/result seam now carries tm_mining's own mining_work_t/
// mining_result_t (see stratum_work_seam.h) -- a received mining.notify is
// parsed AND composed into a ready-to-hash mining_work_t (build_work(),
// work_build.h) before crossing the seam.
#include "unity.h"
#include "stratum_fsm.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Fake transport (stratum_transport_ops_t) -- in-test double, NOT
// bb_tcp_client. See stratum_transport.h's header comment for why the FSM
// is behind a small ops vtable rather than calling bb_tcp_client directly.
// ---------------------------------------------------------------------------

typedef struct {
    bool        connect_result;
    int         connect_calls;
    char        writes[8][300];
    int         write_count;
    const char *pending_lines[8];
    int         pending_count;
    int         pending_idx;
    bool        io_error;
    bool        closed;
    int         close_calls;
} fake_transport_t;

static fake_transport_t s_ft;

static bool fake_connect(void *ctx, const char *host, uint16_t port)
{
    (void)ctx; (void)host; (void)port;
    s_ft.connect_calls++;
    s_ft.closed = false;
    return s_ft.connect_result;
}

static stratum_io_result_t fake_read_line(void *ctx, char *buf, size_t cap, uint32_t poll_ms)
{
    (void)ctx; (void)poll_ms;
    if (s_ft.io_error) return STRATUM_IO_ERROR;
    if (s_ft.pending_idx < s_ft.pending_count) {
        strncpy(buf, s_ft.pending_lines[s_ft.pending_idx++], cap - 1);
        buf[cap - 1] = '\0';
        return STRATUM_IO_OK;
    }
    return STRATUM_IO_TIMEOUT;
}

static bool fake_write(void *ctx, const char *msg)
{
    (void)ctx;
    if (s_ft.write_count < 8) {
        strncpy(s_ft.writes[s_ft.write_count], msg, 299);
        s_ft.writes[s_ft.write_count][299] = '\0';
    }
    s_ft.write_count++;
    return true;
}

static void fake_close(void *ctx)
{
    (void)ctx;
    s_ft.closed = true;
    s_ft.close_calls++;
}

// ---------------------------------------------------------------------------
// Fake work seam (stratum_work_ops_t) -- TA-562's gated production impl is
// a bb_bqueue mailbox/MPSC pair; this fake is exactly the "trivial host
// fake" the seam is designed to make possible.
// ---------------------------------------------------------------------------

typedef struct {
    int              publish_calls;
    int              drain_calls;
    int              reset_calls;
    bool             have_result;
    mining_result_t  queued_result;
    mining_work_t    last_published;
} fake_work_t;

static fake_work_t s_fw;

static bool fake_publish(void *ctx, const mining_work_t *work)
{
    (void)ctx;
    s_fw.publish_calls++;
    s_fw.last_published = *work;
    return true;
}

static bool fake_drain(void *ctx, mining_result_t *out)
{
    (void)ctx;
    if (s_fw.have_result) {
        *out = s_fw.queued_result;
        s_fw.have_result = false;
        s_fw.drain_calls++;
        return true;
    }
    return false;
}

static void fake_reset(void *ctx)
{
    (void)ctx;
    s_fw.reset_calls++;
}

// ---------------------------------------------------------------------------
// Fixture helpers
// ---------------------------------------------------------------------------

static stratum_transport_ops_t s_tops;
static stratum_work_ops_t      s_wops;

static void reset_fakes(void)
{
    memset(&s_ft, 0, sizeof(s_ft));
    memset(&s_fw, 0, sizeof(s_fw));
    s_ft.connect_result = true;
}

static void make_ctx(stratum_fsm_ctx_t *ctx)
{
    s_tops.ctx = NULL;
    s_tops.connect = fake_connect;
    s_tops.read_line = fake_read_line;
    s_tops.write = fake_write;
    s_tops.close = fake_close;

    s_wops.ctx = NULL;
    s_wops.publish = fake_publish;
    s_wops.drain = fake_drain;
    s_wops.reset = fake_reset;

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
    stratum_fsm_init(ctx, &cfg);
}

// Drives DISCONNECTED -> CONNECTING -> CONFIGURE -> SUBSCRIBE (queues the
// subscribe response) in preparation for a test that continues from there.
// `start_now` must be >= any `now_ms` previously fed to this ctx (the FSM's
// timers use unsigned-subtraction elapsed time -- going backwards wraps).
static uint32_t drive_to_subscribe(stratum_fsm_ctx_t *ctx, uint32_t start_now)
{
    uint32_t now = start_now;
    stratum_fsm_service(ctx, now);  // BACKOFF_ELAPSED -> CONNECTING -> TCP_CONNECTED -> CONFIGURE
    now += 1;
    stratum_fsm_service(ctx, now);  // CONFIGURE_DONE -> SUBSCRIBE (sends subscribe, id=2)
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_SUBSCRIBE, stratum_fsm_state(ctx));
    return now;
}

static uint32_t drive_to_running(stratum_fsm_ctx_t *ctx, uint32_t start_now)
{
    uint32_t now = drive_to_subscribe(ctx, start_now);

    s_ft.pending_lines[s_ft.pending_count++] =
        "{\"id\":2,\"result\":[[\"mining.set_difficulty\",\"sub-1\"],\"08000002\",4]}";
    now += 1;
    stratum_fsm_service(ctx, now);  // reads subscribe response -> SUBSCRIBE_OK -> AUTHORIZE (sends id=3)
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_AUTHORIZE, stratum_fsm_state(ctx));

    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":3,\"result\":true}";
    now += 1;
    stratum_fsm_service(ctx, now);  // reads authorize response -> AUTHORIZE_OK -> RUNNING
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_RUNNING, stratum_fsm_state(ctx));
    TEST_ASSERT_TRUE(stratum_fsm_is_connected(ctx));
    return now;
}

// ---------------------------------------------------------------------------
// Transition table coverage
// ---------------------------------------------------------------------------

void test_stratum_fsm_initial_state_is_disconnected(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx));
    TEST_ASSERT_FALSE(stratum_fsm_is_connected(&ctx));
}

void test_stratum_fsm_backoff_elapsed_drives_connect(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);

    stratum_fsm_service(&ctx, 1);
    TEST_ASSERT_EQUAL_INT(1, s_ft.connect_calls);
    // Successful connect + fire-and-forget configure lands us in SUBSCRIBE
    // after ONE more tick (see drive_to_subscribe).
}

void test_stratum_fsm_full_happy_path_to_running(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    drive_to_running(&ctx, 1);

    // configure(id1) + subscribe(id2) + authorize(id3) each wrote one request.
    TEST_ASSERT_EQUAL_INT(3, s_ft.write_count);
}

void test_stratum_fsm_tcp_connect_failure_tears_down(void)
{
    reset_fakes();
    s_ft.connect_result = false;
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);

    stratum_fsm_service(&ctx, 1);  // BACKOFF_ELAPSED -> CONNECTING -> TCP_FAILED -> teardown -> DISCONNECTED

    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx));
    TEST_ASSERT_FALSE(stratum_fsm_is_connected(&ctx));
    TEST_ASSERT_EQUAL_INT(1, s_ft.close_calls);
    TEST_ASSERT_EQUAL_UINT32(1, ctx.reconnect_count);
    TEST_ASSERT_EQUAL_INT(1, ctx.backoff.fail_count);
}

void test_stratum_fsm_job_received_self_loop_publishes_work(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    s_ft.pending_lines[s_ft.pending_count++] =
        "{\"method\":\"mining.notify\",\"params\":"
        "[\"job-1\",\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\",\"cafecafe\",[],\"20000000\",\"1a0392a3\",\"67b1c400\",true]}";
    now += 1;
    stratum_fsm_service(&ctx, now);

    TEST_ASSERT_EQUAL_INT(STRATUM_ST_RUNNING, stratum_fsm_state(&ctx));  // SAME transition
    TEST_ASSERT_EQUAL_INT(1, s_fw.publish_calls);
}

// A mid-session mining.set_difficulty invalidates the target baked into the
// already-staged job -- the FSM must rebuild + republish work (marked clean)
// immediately rather than waiting for the next mining.notify (otherwise the
// miner keeps hashing against the stale target and misjudges every share).
void test_stratum_fsm_set_difficulty_republishes_work_when_job_staged(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    s_ft.pending_lines[s_ft.pending_count++] =
        "{\"method\":\"mining.notify\",\"params\":"
        "[\"job-1\",\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\",\"cafecafe\",[],\"20000000\",\"1a0392a3\",\"67b1c400\",true]}";
    now += 1;
    stratum_fsm_service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(1, s_fw.publish_calls);

    s_ft.pending_lines[s_ft.pending_count++] = "{\"method\":\"mining.set_difficulty\",\"params\":[1024]}";
    now += 1;
    stratum_fsm_service(&ctx, now);

    TEST_ASSERT_EQUAL_INT(2, s_fw.publish_calls);
    TEST_ASSERT_TRUE(s_fw.last_published.clean);
    TEST_ASSERT_EQUAL_DOUBLE(1024.0, s_fw.last_published.difficulty);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_RUNNING, stratum_fsm_state(&ctx));
}

// No job staged yet -- a set_difficulty must update the stored difficulty
// only, with nothing to rebuild/republish.
void test_stratum_fsm_set_difficulty_no_publish_when_no_job_staged(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    s_ft.pending_lines[s_ft.pending_count++] = "{\"method\":\"mining.set_difficulty\",\"params\":[1024]}";
    now += 1;
    stratum_fsm_service(&ctx, now);

    TEST_ASSERT_EQUAL_INT(0, s_fw.publish_calls);
}

void test_stratum_fsm_share_submitted_drains_and_sends(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    s_fw.have_result = true;
    strncpy(s_fw.queued_result.job_id, "job-1", sizeof(s_fw.queued_result.job_id) - 1);
    strncpy(s_fw.queued_result.extranonce2_hex, "00000001", sizeof(s_fw.queued_result.extranonce2_hex) - 1);
    strncpy(s_fw.queued_result.ntime_hex, "67b1c400", sizeof(s_fw.queued_result.ntime_hex) - 1);
    strncpy(s_fw.queued_result.nonce_hex, "01020304", sizeof(s_fw.queued_result.nonce_hex) - 1);

    int writes_before = s_ft.write_count;
    now += 1;
    stratum_fsm_service(&ctx, now);

    TEST_ASSERT_EQUAL_INT(1, s_fw.drain_calls);
    TEST_ASSERT_EQUAL_INT(writes_before + 1, s_ft.write_count);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_RUNNING, stratum_fsm_state(&ctx));
}

void test_stratum_fsm_reconnect_requested_from_running_tears_down(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    drive_to_running(&ctx, 1);

    stratum_fsm_request_reconnect(&ctx);
    stratum_fsm_service(&ctx, 100);

    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx));
    TEST_ASSERT_FALSE(stratum_fsm_is_connected(&ctx));
}

void test_stratum_fsm_io_error_during_subscribe_tears_down(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_subscribe(&ctx, 1);

    s_ft.io_error = true;
    now += 1;
    stratum_fsm_service(&ctx, now);

    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx));
}

void test_stratum_fsm_authorize_rejected_tears_down(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_subscribe(&ctx, 1);

    s_ft.pending_lines[s_ft.pending_count++] =
        "{\"id\":2,\"result\":[[\"mining.set_difficulty\",\"sub-1\"],\"08000002\",4]}";
    now += 1;
    stratum_fsm_service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_AUTHORIZE, stratum_fsm_state(&ctx));

    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":3,\"result\":false,\"error\":[24,\"bad worker\",\"\"]}";
    now += 1;
    stratum_fsm_service(&ctx, now);

    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx));
    TEST_ASSERT_FALSE(stratum_fsm_is_connected(&ctx));
}

// ---------------------------------------------------------------------------
// TA-530: single-funnel teardown
// ---------------------------------------------------------------------------

void test_stratum_fsm_teardown_is_single_funnel_from_every_failure_path(void)
{
    // Three independent failure paths (TCP connect failure, subscribe
    // handshake timeout, an authorized-session IO error) all converge on
    // the identical observable teardown effects: connected=false, the
    // transport closed exactly once, the work seam reset exactly once, and
    // reconnect_count bumped exactly once -- because every failing row in
    // the table shares the ONE action_teardown function (see stratum_fsm.c).
    reset_fakes();
    stratum_fsm_ctx_t ctx_a;
    make_ctx(&ctx_a);
    s_ft.connect_result = false;
    stratum_fsm_service(&ctx_a, 1);
    TEST_ASSERT_FALSE(stratum_fsm_is_connected(&ctx_a));
    TEST_ASSERT_EQUAL_INT(1, s_ft.close_calls);
    TEST_ASSERT_EQUAL_UINT32(1, ctx_a.reconnect_count);

    reset_fakes();
    stratum_fsm_ctx_t ctx_b;
    make_ctx(&ctx_b);
    uint32_t now = drive_to_subscribe(&ctx_b, 1);
    now += STRATUM_HANDSHAKE_TIMEOUT_MS + 1;
    stratum_fsm_service(&ctx_b, now);  // handshake timeout -> teardown
    TEST_ASSERT_FALSE(stratum_fsm_is_connected(&ctx_b));
    TEST_ASSERT_EQUAL_INT(1, s_ft.close_calls);
    TEST_ASSERT_EQUAL_UINT32(1, ctx_b.reconnect_count);
    TEST_ASSERT_EQUAL_INT(1, s_fw.reset_calls);

    reset_fakes();
    stratum_fsm_ctx_t ctx_c;
    make_ctx(&ctx_c);
    now = drive_to_running(&ctx_c, 1);
    s_ft.io_error = true;
    now += 1;
    stratum_fsm_service(&ctx_c, now);  // IO error while RUNNING -> teardown
    TEST_ASSERT_FALSE(stratum_fsm_is_connected(&ctx_c));
    TEST_ASSERT_EQUAL_INT(1, s_ft.close_calls);
    TEST_ASSERT_EQUAL_UINT32(1, ctx_c.reconnect_count);
    TEST_ASSERT_EQUAL_INT(1, s_fw.reset_calls);
}

// ---------------------------------------------------------------------------
// TA-232: backoff cap (<=30s) + reset only on a successful HANDSHAKE
// ---------------------------------------------------------------------------

void test_stratum_fsm_backoff_bumps_on_bare_connect_failure_not_reset(void)
{
    reset_fakes();
    s_ft.connect_result = false;
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);

    stratum_fsm_service(&ctx, 1);
    TEST_ASSERT_EQUAL_INT(1, ctx.backoff.fail_count);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_INITIAL_MS * 2, ctx.backoff.delay_ms);
}

void test_stratum_fsm_backoff_caps_at_30s_across_repeated_failures(void)
{
    reset_fakes();
    s_ft.connect_result = false;
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);

    uint32_t now = 1;
    for (int i = 0; i < 8; i++) {
        stratum_fsm_service(&ctx, now);           // DISCONNECTED -> CONNECTING -> TCP_FAILED -> DISCONNECTED
        now += ctx.next_delay_ms + 1;              // wait out the armed backoff, then retry
        stratum_fsm_service(&ctx, now);
    }
    TEST_ASSERT_TRUE(ctx.backoff.delay_ms <= STRATUM_BACKOFF_CAP_MS);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_CAP_MS, ctx.backoff.delay_ms);
}

void test_stratum_fsm_backoff_resets_only_on_successful_handshake(void)
{
    reset_fakes();
    s_ft.connect_result = false;
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);

    // Two bare TCP failures build up backoff.
    stratum_fsm_service(&ctx, 1);
    uint32_t now = 1 + ctx.next_delay_ms + 1;
    stratum_fsm_service(&ctx, now);
    now = now + ctx.next_delay_ms + 1;
    TEST_ASSERT_EQUAL_INT(2, ctx.backoff.fail_count);
    TEST_ASSERT_TRUE(ctx.backoff.delay_ms > STRATUM_BACKOFF_INITIAL_MS);

    // Now let a connect succeed and the full handshake complete.
    s_ft.connect_result = true;
    now = drive_to_running(&ctx, now);
    (void)now;

    TEST_ASSERT_EQUAL_INT(0, ctx.backoff.fail_count);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_INITIAL_MS, ctx.backoff.delay_ms);
}

// ---------------------------------------------------------------------------
// Drought timers
// ---------------------------------------------------------------------------

void test_stratum_fsm_job_drought_tears_down_after_five_minutes(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    now += STRATUM_WATCHDOG_JOB_DROUGHT_MS + 1;
    stratum_fsm_service(&ctx, now);

    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx));
}

void test_stratum_fsm_job_received_resets_drought_clock(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    // Just under the drought threshold, a fresh job arrives...
    now += STRATUM_WATCHDOG_JOB_DROUGHT_MS - 10;
    s_ft.pending_lines[s_ft.pending_count++] =
        "{\"method\":\"mining.notify\",\"params\":"
        "[\"job-2\",\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\",\"cafecafe\",[],\"20000000\",\"1a0392a3\",\"67b1c400\",true]}";
    stratum_fsm_service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_RUNNING, stratum_fsm_state(&ctx));

    // ...so the drought clock restarts: another (threshold - 10) ms later
    // must NOT trip the watchdog, even though the ORIGINAL session-entry
    // time is now far more than the threshold in the past.
    now += STRATUM_WATCHDOG_JOB_DROUGHT_MS - 10;
    stratum_fsm_service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_RUNNING, stratum_fsm_state(&ctx));
}

void test_stratum_fsm_share_drought_tears_down_after_thirty_minutes(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    now += STRATUM_WATCHDOG_SHARE_DROUGHT_MS + 1;
    stratum_fsm_service(&ctx, now);

    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx));
}

void test_stratum_fsm_keepalive_fires_after_ninety_seconds(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    int writes_before = s_ft.write_count;
    now += STRATUM_WATCHDOG_KEEPALIVE_MS + 1;
    stratum_fsm_service(&ctx, now);

    TEST_ASSERT_EQUAL_INT(writes_before + 1, s_ft.write_count);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_RUNNING, stratum_fsm_state(&ctx));  // keepalive is a SAME transition
}

// A transport write (e.g. a share submit) re-arms the keepalive-due timer,
// so keepalive is suppressed while other traffic is flowing and only fires
// after a genuinely idle interval measured from the LAST write.
void test_stratum_fsm_keepalive_suppressed_by_recent_tx_then_fires_after_idle(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    // Just before the original keepalive schedule would fire, a share
    // submit sends its own transport write and re-arms the timer.
    now += STRATUM_WATCHDOG_KEEPALIVE_MS - 1000;
    s_fw.have_result = true;
    strncpy(s_fw.queued_result.job_id, "job-1", sizeof(s_fw.queued_result.job_id) - 1);
    strncpy(s_fw.queued_result.extranonce2_hex, "00000001", sizeof(s_fw.queued_result.extranonce2_hex) - 1);
    strncpy(s_fw.queued_result.ntime_hex, "67b1c400", sizeof(s_fw.queued_result.ntime_hex) - 1);
    strncpy(s_fw.queued_result.nonce_hex, "01020304", sizeof(s_fw.queued_result.nonce_hex) - 1);
    stratum_fsm_service(&ctx, now);
    int writes_before = s_ft.write_count;

    // The ORIGINAL session-entry schedule (session start + KEEPALIVE_MS) has
    // now elapsed, but the share write reset the clock -- no keepalive
    // fires yet.
    now += 1001;
    stratum_fsm_service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(writes_before, s_ft.write_count);
    TEST_ASSERT_EQUAL_INT(STRATUM_ST_RUNNING, stratum_fsm_state(&ctx));

    // A full idle interval after the LAST write, keepalive does fire.
    now += STRATUM_WATCHDOG_KEEPALIVE_MS;
    stratum_fsm_service(&ctx, now);
    TEST_ASSERT_EQUAL_INT(writes_before + 1, s_ft.write_count);
}

// ---------------------------------------------------------------------------
// Keepalive-id tracking: a delayed keepalive ack must never inflate the
// accepted/rejected share counters (known pre-rebuild bug).
// ---------------------------------------------------------------------------

void test_stratum_fsm_keepalive_ack_does_not_inflate_share_counters(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    now += STRATUM_WATCHDOG_KEEPALIVE_MS + 1;
    stratum_fsm_service(&ctx, now);  // sends keepalive; id is next_msg_id at send time (4)

    TEST_ASSERT_EQUAL_UINT32(0, ctx.accepted);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.rejected);

    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":4,\"result\":true}";
    now += 1;
    stratum_fsm_service(&ctx, now);

    // The keepalive ack (result:true, matching a registered KEEPALIVE id)
    // must be classified as a keepalive ack, NOT counted as an accepted
    // share.
    TEST_ASSERT_EQUAL_UINT32(0, ctx.accepted);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.rejected);
}

void test_stratum_fsm_unregistered_id_response_counts_as_submit(void)
{
    reset_fakes();
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    uint32_t now = drive_to_running(&ctx, 1);

    // A response id that was never registered (e.g. a genuine mining.submit
    // ack whose id we didn't separately track in this simplified test path)
    // falls through to the generic accept/reject counters.
    s_ft.pending_lines[s_ft.pending_count++] = "{\"id\":999,\"result\":true}";
    now += 1;
    stratum_fsm_service(&ctx, now);

    TEST_ASSERT_EQUAL_UINT32(1, ctx.accepted);
}
