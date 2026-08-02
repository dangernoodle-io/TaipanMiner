// tm_compose (TA-562): the genuinely-new integration under test
// is the two ops tables (mining_queue_ops_t and tm_pool_work_ops_t) meeting
// at the SAME pair of bb_bqueue instances -- one capacity-1 work mailbox,
// one capacity-16 result MPSC.
//
// The first block below exercises that EXACT bb_bqueue call set
// (peek_work -> peek(0), post_result -> send(0), publish -> overwrite(),
// drain -> receive(0), reset -> reset()) against real host-backed queues,
// proving the mailbox/MPSC contract each side depends on.
//
// The second block (test_tm_compose_mining_stratum_init_*) calls the REAL
// tm_compose_mining_stratum_init() (the unconfigured-slot early-return path,
// and the configured-slot path's queue/ops wiring -- the bb_task_create/
// pool-client-init block is ESP_PLATFORM-gated and not reachable on host)
// and drives the adapters it actually binds -- via mining_work_peek()/
// mining_result_post() for the mining side, and via
// tm_compose_mining_stratum_test_work_ops() (TM_COMPOSE_MINING_STRATUM_TESTING
// only) for the pool-client side -- proving the REAL adapter-to-queue
// mapping (not a re-implementation of it).
//
// BB_BQUEUE_MAX_INSTANCES defaults to 2 -- each test below destroys its own
// two queues (directly, or via tm_compose_mining_stratum_test_reset()) before
// returning so the next test starts from a clean pool.
#include "unity.h"
#include "bb_bqueue.h"
#include "bb_storage.h"
#include "mining.h"
#include "tm_compose.h"
#include "tm_pool_cfg.h"
#include "tm_pool_policy.h"
#include "fake_nvs_backend.h"

#include <string.h>

static void make_queues(bb_bqueue_t *work_q, bb_bqueue_t *result_q)
{
    bb_bqueue_cfg_t work_cfg   = { .capacity = 1,  .item_bytes = sizeof(mining_work_t),   .name = "tm_work" };
    bb_bqueue_cfg_t result_cfg = { .capacity = 16, .item_bytes = sizeof(mining_result_t), .name = "tm_result" };

    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_create(&work_cfg, work_q));
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_create(&result_cfg, result_q));
}

static mining_work_t make_work(uint32_t work_seq)
{
    mining_work_t w;
    memset(&w, 0, sizeof(w));
    w.work_seq = work_seq;
    strncpy(w.job_id, "job", sizeof(w.job_id) - 1);
    return w;
}

static mining_result_t make_result(const char *job_id)
{
    mining_result_t r;
    memset(&r, 0, sizeof(r));
    strncpy(r.job_id, job_id, sizeof(r.job_id) - 1);
    return r;
}

/* ---------------------------------------------------------------------------
 * publish() (overwrite) -> peek_work() (peek) round-trips a mining_work_t
 * ---------------------------------------------------------------------------*/
void test_tm_compose_publish_then_peek_round_trips_work(void)
{
    bb_bqueue_t work_q = NULL, result_q = NULL;
    make_queues(&work_q, &result_q);

    mining_work_t in = make_work(42);
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_overwrite(work_q, &in));

    mining_work_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_peek(work_q, &out, 0));
    TEST_ASSERT_EQUAL_UINT32(42, out.work_seq);
    TEST_ASSERT_EQUAL_STRING("job", out.job_id);

    bb_bqueue_destroy(work_q);
    bb_bqueue_destroy(result_q);
}

/* ---------------------------------------------------------------------------
 * The mailbox keeps only the latest value: two publishes, one peek sees the
 * 2nd.
 * ---------------------------------------------------------------------------*/
void test_tm_compose_mailbox_keeps_only_latest_value(void)
{
    bb_bqueue_t work_q = NULL, result_q = NULL;
    make_queues(&work_q, &result_q);

    mining_work_t first  = make_work(1);
    mining_work_t second = make_work(2);
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_overwrite(work_q, &first));
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_overwrite(work_q, &second));

    mining_work_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_peek(work_q, &out, 0));
    TEST_ASSERT_EQUAL_UINT32(2, out.work_seq);

    bb_bqueue_destroy(work_q);
    bb_bqueue_destroy(result_q);
}

/* ---------------------------------------------------------------------------
 * post_result() (send) -> drain() (receive) round-trips a mining_result_t
 * and drains in FIFO order.
 * ---------------------------------------------------------------------------*/
void test_tm_compose_post_result_then_drain_round_trips_in_order(void)
{
    bb_bqueue_t work_q = NULL, result_q = NULL;
    make_queues(&work_q, &result_q);

    mining_result_t r1 = make_result("job-a");
    mining_result_t r2 = make_result("job-b");
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_send(result_q, &r1, 0));
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_send(result_q, &r2, 0));

    mining_result_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_receive(result_q, &out, 0));
    TEST_ASSERT_EQUAL_STRING("job-a", out.job_id);

    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_receive(result_q, &out, 0));
    TEST_ASSERT_EQUAL_STRING("job-b", out.job_id);

    bb_bqueue_destroy(work_q);
    bb_bqueue_destroy(result_q);
}

/* ---------------------------------------------------------------------------
 * post_result() (send) drops (returns false, per the drop-on-full contract)
 * once the capacity-16 result queue is full.
 * ---------------------------------------------------------------------------*/
void test_tm_compose_result_queue_drops_at_capacity(void)
{
    bb_bqueue_t work_q = NULL, result_q = NULL;
    make_queues(&work_q, &result_q);

    for (int i = 0; i < 16; i++) {
        mining_result_t r = make_result("job");
        TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_send(result_q, &r, 0));
    }

    mining_result_t overflow = make_result("job-overflow");
    TEST_ASSERT_EQUAL(BB_ERR_NO_SPACE, bb_bqueue_send(result_q, &overflow, 0));

    bb_bqueue_destroy(work_q);
    bb_bqueue_destroy(result_q);
}

/* ---------------------------------------------------------------------------
 * reset() clears the work mailbox only (mailbox-mode-only primitive; the
 * result queue has no reset -- see tm_compose_mining_stratum.c's wire_reset_
 * work() doc comment).
 * ---------------------------------------------------------------------------*/
void test_tm_compose_reset_clears_work_mailbox(void)
{
    bb_bqueue_t work_q = NULL, result_q = NULL;
    make_queues(&work_q, &result_q);

    mining_work_t w = make_work(7);
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_overwrite(work_q, &w));
    TEST_ASSERT_EQUAL(BB_OK, bb_bqueue_reset(work_q));

    mining_work_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(BB_ERR_NOT_FOUND, bb_bqueue_peek(work_q, &out, 0));

    bb_bqueue_destroy(work_q);
    bb_bqueue_destroy(result_q);
}

/* ---------------------------------------------------------------------------
 * tm_compose_mining_stratum_init() itself -- unconfigured-slot early return and
 * the configured-slot adapter-to-queue mapping.
 * ---------------------------------------------------------------------------*/

static void reset_all(void)
{
    bb_storage_test_reset();
    fake_nvs_reset();
    bb_storage_register_backend("nvs", &s_fake_nvs_vtable, NULL);
    mining_set_queue_ops(NULL);
    tm_compose_mining_stratum_test_reset();
}

static void configure_pool(uint8_t idx, const char *host)
{
    tm_pool_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.host, host, sizeof(cfg.host) - 1);
    cfg.port = 3333;
    strncpy(cfg.wallet, "bc1qexamplewallet", sizeof(cfg.wallet) - 1);
    strncpy(cfg.worker, "rig01", sizeof(cfg.worker) - 1);
    strncpy(cfg.pass, "x", sizeof(cfg.pass) - 1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(idx, &cfg));
}

void test_tm_compose_mining_stratum_init_unconfigured_slot_returns_ok_and_wires_nothing(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());  // no slots configured -> active_idx=0, unconfigured

    TEST_ASSERT_EQUAL(BB_OK, tm_compose_mining_stratum_init());

    // Early-return path: the pool-client work_ops are never bound.
    const tm_pool_work_ops_t *ops = tm_compose_mining_stratum_test_work_ops();
    TEST_ASSERT_NULL(ops->publish);
    TEST_ASSERT_NULL(ops->drain);
    TEST_ASSERT_NULL(ops->reset);

    // The queues + mining ops ARE bound (mining_set_queue_ops ran before the
    // is_configured gate) -- but nothing has published, so peek reports no
    // work.
    mining_work_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_FALSE(mining_work_peek(&out));

    mining_set_queue_ops(NULL);
    tm_compose_mining_stratum_test_reset();
}

void test_tm_compose_mining_stratum_init_configured_slot_binds_pool_client_ops_to_same_queues(void)
{
    reset_all();
    configure_pool(0, "pool.example.com");
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_policy_init());  // MANUAL default -> active_idx=0 (now configured)

    // Host build: the bb_task_create/pool-client-init block is
    // ESP_PLATFORM-gated, so init() returns BB_OK from the tail return
    // without spawning anything -- only the queue/ops wiring above that
    // block is exercised here.
    TEST_ASSERT_EQUAL(BB_OK, tm_compose_mining_stratum_init());

    const tm_pool_work_ops_t *ops = tm_compose_mining_stratum_test_work_ops();
    TEST_ASSERT_NOT_NULL(ops->publish);
    TEST_ASSERT_NOT_NULL(ops->drain);
    TEST_ASSERT_NOT_NULL(ops->reset);

    // publish() (pool-client side, the REAL bound adapter) -> peek_work()
    // (mining side, via mining_work_peek()) round-trips through the SAME
    // work queue.
    mining_work_t work = make_work(99);
    TEST_ASSERT_TRUE(ops->publish(ops->ctx, &work));

    mining_work_t peeked;
    memset(&peeked, 0, sizeof(peeked));
    TEST_ASSERT_TRUE(mining_work_peek(&peeked));
    TEST_ASSERT_EQUAL_UINT32(99, peeked.work_seq);

    // post_result() (mining side, via mining_result_post()) -> drain()
    // (pool-client side, the REAL bound adapter) round-trips through the
    // SAME result queue.
    mining_result_t result = make_result("job-x");
    TEST_ASSERT_TRUE(mining_result_post(&result));

    mining_result_t drained;
    memset(&drained, 0, sizeof(drained));
    TEST_ASSERT_TRUE(ops->drain(ops->ctx, &drained));
    TEST_ASSERT_EQUAL_STRING("job-x", drained.job_id);

    // reset() (pool-client side) clears the SAME work mailbox
    // mining_work_peek() reads.
    ops->reset(ops->ctx);
    mining_work_t after_reset;
    memset(&after_reset, 0, sizeof(after_reset));
    TEST_ASSERT_FALSE(mining_work_peek(&after_reset));

    mining_set_queue_ops(NULL);
    tm_compose_mining_stratum_test_reset();
}
