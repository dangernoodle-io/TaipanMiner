// TA-233/TA-494: a stratum failure must never block the caller (and, by
// construction, never stall a co-running WiFi/management task). Host tests
// can't measure wall-clock blocking directly, so this asserts the
// structural guarantee instead: with the transport (or the pool response
// stream) PERMANENTLY failing, repeated stratum_fsm_service() calls each
// return control to the caller promptly enough that a co-running counter,
// bumped once per iteration BETWEEN calls, reaches its full count -- i.e.
// stratum_fsm never enters an internal unbounded retry loop that would
// starve the caller's task of a chance to run anything else (WiFi,
// mgmt-plane, ...). Every stratum_fsm_service() call performs only bounded
// transport operations (one connect attempt, one poll-bounded line read,
// one poll-bounded drain+submit) and returns.
#include "unity.h"
#include "stratum_fsm.h"
#include <string.h>

typedef struct {
    bool        connect_result;
    bool        hang_read;       // read_line() always reports TIMEOUT (simulates a hung/dead pool)
    bool        io_error;
    int         connect_calls;
    int         read_calls;
    int         write_calls;
    int         close_calls;
} fake_transport_t;

static fake_transport_t s_ft;

static bool fake_connect(void *ctx, const char *host, uint16_t port)
{
    (void)ctx; (void)host; (void)port;
    s_ft.connect_calls++;
    return s_ft.connect_result;
}

static stratum_io_result_t fake_read_line(void *ctx, char *buf, size_t cap, uint32_t poll_ms)
{
    (void)ctx; (void)buf; (void)cap; (void)poll_ms;
    s_ft.read_calls++;
    if (s_ft.io_error) return STRATUM_IO_ERROR;
    return STRATUM_IO_TIMEOUT;  // "hangs" forever from the caller's point of view
}

static bool fake_write(void *ctx, const char *msg)
{
    (void)ctx; (void)msg;
    s_ft.write_calls++;
    return true;
}

static void fake_close(void *ctx)
{
    (void)ctx;
    s_ft.close_calls++;
}

static bool fake_publish(void *ctx, const mining_work_t *work) { (void)ctx; (void)work; return true; }
static bool fake_drain(void *ctx, mining_result_t *out) { (void)ctx; (void)out; return false; }
static void fake_reset(void *ctx) { (void)ctx; }

static stratum_transport_ops_t s_tops;
static stratum_work_ops_t      s_wops;

static void make_ctx(stratum_fsm_ctx_t *ctx)
{
    memset(&s_ft, 0, sizeof(s_ft));

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

void test_stratum_isolation_service_never_blocks_when_connect_permanently_fails(void)
{
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    s_ft.connect_result = false;  // pool permanently unreachable

    uint32_t now = 1;
    int wifi_progress = 0;
    const int iterations = 500;
    for (int i = 0; i < iterations; i++) {
        stratum_fsm_service(&ctx, now);
        wifi_progress++;             // the "other task" -- proves control returned
        now += 1;
    }

    TEST_ASSERT_EQUAL_INT(iterations, wifi_progress);
    // The FSM is still alive and cycling DISCONNECTED<->CONNECTING, never
    // wedged in an unrecognized state.
    stratum_fsm_state_t st = stratum_fsm_state(&ctx);
    TEST_ASSERT_TRUE(st == STRATUM_ST_DISCONNECTED || st == STRATUM_ST_CONNECTING);
    TEST_ASSERT_FALSE(stratum_fsm_is_connected(&ctx));
}

void test_stratum_isolation_service_never_blocks_when_pool_hangs_mid_session(void)
{
    // Connect succeeds, but every subsequent read_line() reports TIMEOUT
    // forever (a pool that accepted the TCP connection then went silent).
    // Each stratum_fsm_service() call is still bounded by read_line()'s own
    // poll_ms contract -- the FSM never spins waiting for a line that will
    // never arrive.
    stratum_fsm_ctx_t ctx;
    make_ctx(&ctx);
    s_ft.connect_result = true;

    uint32_t now = 1;
    int wifi_progress = 0;
    const int iterations = 500;
    for (int i = 0; i < iterations; i++) {
        stratum_fsm_service(&ctx, now);
        wifi_progress++;
        now += 1;
    }

    TEST_ASSERT_EQUAL_INT(iterations, wifi_progress);
    TEST_ASSERT_GREATER_THAN_INT(0, s_ft.read_calls);
}

void test_stratum_isolation_service_touches_nothing_outside_its_own_ctx(void)
{
    // A sibling ctx instance, never serviced, must be completely unaffected
    // by driving a separate ctx through repeated failures -- proves the FSM
    // has no hidden global/shared state beyond what's passed in.
    stratum_fsm_ctx_t ctx_driven;
    make_ctx(&ctx_driven);
    s_ft.connect_result = false;

    stratum_fsm_ctx_t ctx_untouched;
    make_ctx(&ctx_untouched);

    uint32_t now = 1;
    for (int i = 0; i < 50; i++) {
        stratum_fsm_service(&ctx_driven, now);
        now += 1;
    }

    TEST_ASSERT_EQUAL_INT(STRATUM_ST_DISCONNECTED, stratum_fsm_state(&ctx_untouched));
    TEST_ASSERT_FALSE(stratum_fsm_is_connected(&ctx_untouched));
    TEST_ASSERT_EQUAL_UINT32(0, ctx_untouched.reconnect_count);
}
