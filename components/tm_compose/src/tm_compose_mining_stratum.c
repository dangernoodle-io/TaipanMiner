// tm_compose -- TA-562: the first live-mining composition. Binds
// tm_mining's mining_queue_ops_t and tm_pool_client's tm_pool_work_ops_t to
// the SAME pair of bb_bqueue instances (a capacity-1 work mailbox, a
// capacity-16 result MPSC), then -- iff the FSM-selected active pool slot
// is configured -- spawns the mining task and a stratum service task
// driving tm_stratum's V1 pool-client adapter.
//
// File-scope STATIC storage is load-bearing here, not a style choice: the
// FSM borrows pointers out of the tm_pool_cfg_t passed to
// tm_pool_client_ops_t.init() (per tm_pool_client.h's own contract, cfg
// must outlive ctx) and re-reads host/port on every reconnect, so a stack
// local would leave the FSM holding a dangling pointer the moment
// tm_compose_mining_stratum_init() returns.
#include "tm_compose.h"

#include "bb_bqueue.h"
#include "bb_log.h"
#include "mining.h"
#include "tm_pool_client.h"
#include "tm_pool_cfg.h"
#include "tm_pool_policy.h"
#include "stratum_pool_client.h"

#include <string.h>

static const char *TAG = "tm_compose";

// ---------------------------------------------------------------------------
// File-scope static storage (see header comment above for why).
// ---------------------------------------------------------------------------
static bb_bqueue_t                s_work_q;
static bb_bqueue_t                s_result_q;
static tm_pool_cfg_t              s_active_pool_cfg;
static stratum_pool_client_ctx_t  s_pool_client_ctx;
static tm_pool_work_ops_t         s_work_ops;

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
_Static_assert(sizeof(mining_work_t)   <= CONFIG_BB_BQUEUE_MAX_ITEM_BYTES,
    "mining_work_t exceeds bb_bqueue item cap");
_Static_assert(sizeof(mining_result_t) <= CONFIG_BB_BQUEUE_MAX_ITEM_BYTES,
    "mining_result_t exceeds bb_bqueue item cap");
#endif

// ---------------------------------------------------------------------------
// mining_queue_ops_t adapter -- mining.c's peek/post, dispatched onto
// s_work_q/s_result_q. Host-reachable (no platform types) so host tests can
// exercise the binding without a device.
// ---------------------------------------------------------------------------
static bool wire_peek_work(void *ctx, mining_work_t *out)
{
    (void)ctx;
    return bb_bqueue_peek(s_work_q, out, 0) == BB_OK;
}

static bool wire_post_result(void *ctx, const mining_result_t *result)
{
    (void)ctx;
    // Drop-on-full is the contract: bb_bqueue_send() returns BB_ERR_NO_SPACE
    // when the result MPSC is full and timeout_ms == 0 -- callers never
    // block the mining hot loop on delivery.
    return bb_bqueue_send(s_result_q, result, 0) == BB_OK;
}

// ---------------------------------------------------------------------------
// tm_pool_work_ops_t adapter -- the pool-client backend's publish/drain/
// reset, dispatched onto the SAME two queues.
// ---------------------------------------------------------------------------
static bool wire_publish_work(void *ctx, const mining_work_t *work)
{
    (void)ctx;
    return bb_bqueue_overwrite(s_work_q, work) == BB_OK;
}

static bool wire_drain_result(void *ctx, mining_result_t *out)
{
    (void)ctx;
    return bb_bqueue_receive(s_result_q, out, 0) == BB_OK;
}

static void wire_reset_work(void *ctx)
{
    (void)ctx;
    // Mailbox-mode-only: bb_bqueue_reset() is valid on the capacity-1 work
    // queue only. The capacity-16 result queue has no reset primitive (it is
    // bounded-MPSC mode) -- session teardown drains it via normal
    // consumption instead, so there is nothing to reset there.
    bb_bqueue_reset(s_work_q);
}

#ifdef ESP_PLATFORM

#include "bb_clock.h"
#include "bb_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void *s_stratum_task;
static void *s_mining_task;

// 16384, not the 4096-8192 CLAUDE.md convention: service()'s FSM ->
// build_work() call chain puts a ~48-entry JSON tok pool, a ~1KB coinbase
// scratch buffer, and a sha256d pass all on one stack frame on top of the
// FSM's own frame and base task overhead -- 8192 overflowed on first
// mining.notify with a pool configured (bench-confirmed panic).
#define STRATUM_TASK_STACK_BYTES 16384

// Stack HWM log cadence -- every ~30s at the loop's 75ms tick (400 ticks).
// Cheap field diagnostic to right-size STRATUM_TASK_STACK_BYTES above and
// confirm it's not a slow runaway; kept low-rate so it's not log spam.
#define STRATUM_TASK_HWM_LOG_EVERY_N_TICKS 400

static void stratum_service_task(void *arg)
{
    (void)arg;
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();
    uint32_t tick = 0;
    for (;;) {
        uint32_t now_ms = bb_clock_now_ms();
        ops->service(&s_pool_client_ctx, now_ms);

        if (++tick >= STRATUM_TASK_HWM_LOG_EVERY_N_TICKS) {
            tick = 0;
            // uxTaskGetStackHighWaterMark() returns BYTES directly on this
            // ESP-IDF port (its own header doc is explicit that this
            // deviates from "the standard FreeRTOS documentation", which
            // returns words) -- no *4 needed here; verified against the
            // vendored freertos/task.h in this build.
            UBaseType_t hwm_bytes = uxTaskGetStackHighWaterMark(NULL);
            bb_log_i(TAG, "stratum task stack HWM: %u bytes free", (unsigned)hwm_bytes);
        }

        vTaskDelay(pdMS_TO_TICKS(75));
    }
}

#endif // ESP_PLATFORM

bb_err_t tm_compose_mining_stratum_init(void)
{
    bb_bqueue_cfg_t work_cfg   = { .capacity = 1,  .item_bytes = sizeof(mining_work_t),   .name = "tm_work" };
    bb_bqueue_cfg_t result_cfg = { .capacity = 16, .item_bytes = sizeof(mining_result_t), .name = "tm_result" };

    bb_err_t err = bb_bqueue_create(&work_cfg, &s_work_q);
    if (err != BB_OK) {
        bb_log_e(TAG, "bb_bqueue_create(work) failed: %d", (int)err);
        return err;
    }
    err = bb_bqueue_create(&result_cfg, &s_result_q);
    if (err != BB_OK) {
        bb_log_e(TAG, "bb_bqueue_create(result) failed: %d", (int)err);
        return err;
    }

    static const mining_queue_ops_t s_mining_ops = {
        .peek_work   = wire_peek_work,
        .post_result = wire_post_result,
        .ctx         = NULL,
    };
    mining_set_queue_ops(&s_mining_ops);

    uint8_t active_idx = tm_pool_policy_active_idx();

    if (!tm_pool_config_is_configured(active_idx)) {
        bb_log_w(TAG, "no pool configured (slot %u); mining/stratum idle -- provision a pool and reboot",
                 (unsigned)active_idx);
        return BB_OK;
    }

    err = tm_pool_config_get(active_idx, &s_active_pool_cfg);
    if (err != BB_OK) {
        bb_log_e(TAG, "tm_pool_config_get(%u) failed: %d", (unsigned)active_idx, (int)err);
        return err;
    }

    s_work_ops = (tm_pool_work_ops_t){
        .ctx     = NULL,
        .publish = wire_publish_work,
        .drain   = wire_drain_result,
        .reset   = wire_reset_work,
    };

#ifdef ESP_PLATFORM
    const tm_pool_client_ops_t *ops = tm_stratum_v1_pool_client_ops();
    err = ops->init(&s_pool_client_ctx, &s_active_pool_cfg, &s_work_ops);
    if (err != BB_OK) {
        bb_log_e(TAG, "pool client init failed: %d", (int)err);
        return err;
    }

    bb_task_config_t stratum_cfg = {
        .entry       = stratum_service_task,
        .name        = "tm_stratum",
        .arg         = NULL,
        .stack_bytes = STRATUM_TASK_STACK_BYTES,
        .priority    = 10,
        .core        = 0,
        .backing     = BB_TASK_BACKING_DYNAMIC,
        .wdt_arm     = false,
    };
    err = bb_task_create(&stratum_cfg, &s_stratum_task);
    if (err != BB_OK) {
        bb_log_e(TAG, "bb_task_create(tm_stratum) failed: %d", (int)err);
        return err;
    }

    // mining_stats_init() (TA-562 HW bugfix): creates mining_stats.lock,
    // resets the session, and starts the hw-hashrate averaging bb_timer --
    // this was never wired into the composition before, so mining_stats.lock
    // stayed uninitialized and the mining task panicked on its first stats
    // access. mining_task() itself does not call this (confirmed no
    // double-init). Must run before the mining task spawns below.
    mining_stats_init();

    if (g_miner_config.init) {
        g_miner_config.init();
    }

    bb_task_config_t mining_cfg = {
        .entry       = g_miner_config.task_fn,
        .name        = g_miner_config.name,
        .arg         = NULL,
        .stack_bytes = g_miner_config.stack_size,
        .priority    = g_miner_config.priority,
        .core        = g_miner_config.core,
        .backing     = BB_TASK_BACKING_DYNAMIC,
        .wdt_arm     = false,
    };
    err = bb_task_create(&mining_cfg, &s_mining_task);
    if (err != BB_OK) {
        // Accepted tradeoff (see tm_compose_mining_stratum_init()'s "Partial-
        // spawn note" doc comment): bb_task has no destroy/delete API, so
        // the stratum service task spawned just above is left running.
        // Fail loud (this error still propagates) rather than silently limp.
        bb_log_e(TAG, "bb_task_create(%s) failed: %d", g_miner_config.name, (int)err);
        return err;
    }

    bb_log_i(TAG, "mining+stratum wired, pool idx=%u host=%s:%u", (unsigned)active_idx,
             s_active_pool_cfg.host, (unsigned)s_active_pool_cfg.port);
#endif // ESP_PLATFORM

    return BB_OK;
}

#ifdef TM_COMPOSE_MINING_STRATUM_TESTING
void tm_compose_mining_stratum_test_reset(void)
{
    if (s_work_q) {
        bb_bqueue_destroy(s_work_q);
        s_work_q = NULL;
    }
    if (s_result_q) {
        bb_bqueue_destroy(s_result_q);
        s_result_q = NULL;
    }
    memset(&s_active_pool_cfg, 0, sizeof(s_active_pool_cfg));
    memset(&s_pool_client_ctx, 0, sizeof(s_pool_client_ctx));
    memset(&s_work_ops, 0, sizeof(s_work_ops));
}

const tm_pool_work_ops_t *tm_compose_mining_stratum_test_work_ops(void)
{
    return &s_work_ops;
}
#endif /* TM_COMPOSE_MINING_STRATUM_TESTING */
