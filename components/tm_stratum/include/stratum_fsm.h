#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bb_fsm.h"
#include "stratum_machine.h"
#include "stratum_backoff.h"
#include "stratum_reqid.h"
#include "stratum_share.h"
#include "stratum_transport.h"
#include "tm_pool_work_seam.h"  // tm_pool_work_ops_t (tm_pool_client's own seam)

// Table-driven stratum protocol client over bb_fsm and a thin transport ops
// seam (stratum_transport_ops_t) that wraps bb_tcp_client in production.
//
// States: disconnected -> connecting -> configure -> subscribe -> authorize
// -> running, plus a single teardown funnel (action_teardown) shared by
// EVERY failure transition back to disconnected (TA-530).
//
// A received mining.notify is parsed AND turned into a ready-to-hash
// mining_work_t (build_work(), work_build.h) before crossing the work seam
// -- the FSM owns coinbase/merkle/header composition, not just protocol
// parsing.
//
// Driving contract: the shell calls stratum_fsm_service() periodically
// (e.g. every 50-100ms, matching the pre-rebuild task's cadence). Each call
// performs only BOUNDED transport operations (a single connect attempt, one
// poll-bounded line read, one poll-bounded result drain + submit) and
// returns promptly -- it never busy-waits or blocks past its own bounded
// I/O, so a stratum failure (dead pool, DNS failure, TCP hang) can never
// stall the caller's task, let alone WiFi or any other management-plane
// task running elsewhere (TA-233/TA-494). See test_stratum_isolation.c.

typedef enum {
    STRATUM_ST_DISCONNECTED = 0,
    STRATUM_ST_CONNECTING   = 1,
    STRATUM_ST_CONFIGURE    = 2,
    STRATUM_ST_SUBSCRIBE    = 3,
    STRATUM_ST_AUTHORIZE    = 4,
    STRATUM_ST_RUNNING      = 5,
} stratum_fsm_state_t;

typedef enum {
    STRATUM_EV_BACKOFF_ELAPSED      = 0,
    STRATUM_EV_TCP_CONNECTED        = 1,
    STRATUM_EV_TCP_FAILED           = 2,
    STRATUM_EV_CONFIGURE_DONE       = 3,
    STRATUM_EV_SUBSCRIBE_OK         = 4,
    STRATUM_EV_AUTHORIZE_OK         = 5,
    STRATUM_EV_HANDSHAKE_REJECTED   = 6,
    STRATUM_EV_HANDSHAKE_TIMEOUT    = 7,
    STRATUM_EV_JOB_RECEIVED         = 8,
    STRATUM_EV_SHARE_SUBMITTED      = 9,
    STRATUM_EV_KEEPALIVE_DUE        = 10,
    STRATUM_EV_JOB_DROUGHT_TIMEOUT  = 11,
    STRATUM_EV_SHARE_DROUGHT_TIMEOUT = 12,
    STRATUM_EV_IO_ERROR             = 13,
    STRATUM_EV_RECONNECT_REQUESTED  = 14,
    STRATUM_EV_COUNT                = 15,
} stratum_fsm_event_t;

#define STRATUM_HANDSHAKE_TIMEOUT_MS 5000U

// RUNNING-state watchdog intervals -- the FSM arms/re-arms these directly via
// bb_fsm timers (see stratum_fsm.c's on_enter_running() and fsm_arm()); there
// is no separate watchdog-predicate module, only these shared constants.
#define STRATUM_WATCHDOG_JOB_DROUGHT_MS    (5UL * 60 * 1000)   // 5 min
#define STRATUM_WATCHDOG_SHARE_DROUGHT_MS  (30UL * 60 * 1000)  // 30 min
#define STRATUM_WATCHDOG_KEEPALIVE_MS      90000UL             // 90s

// Optional on-accepted-share hook. `ud` is whatever the composition root
// passed to stratum_fsm_set_accepted_share_hook(); `share` is owned by the
// FSM and only valid for the duration of the callback.
typedef void (*stratum_accepted_share_cb)(void *ud, const stratum_accepted_share_t *share);

// Optional on-rejected-share hook, symmetric with stratum_accepted_share_cb
// above but with no payload -- the composition root only needs a count
// signal (mining_stats.session.rejected); the reject reason is already
// logged synchronously by stratum_fsm.c itself.
typedef void (*stratum_rejected_share_cb)(void *ud);

typedef struct {
    const char *host;
    uint16_t    port;
    const char *wallet;
    const char *worker;
    const char *pass;
    bool        extranonce_subscribe;  // send mining.extranonce.subscribe (TA-306)

    stratum_transport_ops_t *transport;  // borrowed, must outlive the ctx
    tm_pool_work_ops_t      *work;       // borrowed, must outlive the ctx
} stratum_fsm_cfg_t;

typedef struct {
    bb_fsm_t fsm;
    bb_fsm_desc_t fsm_desc;   // per-instance copy of the static table desc, with .ctx patched to `this`

    stratum_fsm_cfg_t cfg;

    stratum_state_t         proto;    // pool session state (stratum_machine.h)
    stratum_backoff_t       backoff;
    // In-flight request id -> kind + (for SUBMIT) full share record, one
    // table (stratum_reqid.h) -- see that header's own doc comment for why
    // the share payload lives IN the reqid slot rather than a second table.
    stratum_reqid_table_t   reqids;
    uint32_t                next_delay_ms;   // computed by action_teardown, consumed by on_enter_disconnected
    bool                    first_attempt;
    uint32_t                work_seq;        // monotonic build_work() counter, never reset by teardown

    bool     connected;
    bool     reconnect_requested;
    uint32_t now_ms;                          // stashed by stratum_fsm_service() for hooks/actions to read
    uint32_t timer_armed_at_ms[STRATUM_EV_COUNT];

    // One-shot "a hard read/transport/handshake failure just tore the
    // session down" flag (see action_teardown() in stratum_fsm.c). Set on
    // STRATUM_EV_IO_ERROR / HANDSHAKE_REJECTED / HANDSHAKE_TIMEOUT /
    // JOB_DROUGHT_TIMEOUT / SHARE_DROUGHT_TIMEOUT ONLY -- never on
    // STRATUM_EV_TCP_FAILED (never got a session) or
    // STRATUM_EV_RECONNECT_REQUESTED (an explicit/clean disconnect, e.g.
    // WiFi IP loss). Consumed via stratum_fsm_consumed_read_failure().
    bool hard_failure_pending;

    // Optional on-accepted-share hook (nullable, no-op when unset). tm_stratum
    // never depends on tm_pool_stats or any other delivery/recording sink --
    // the composition root binds this to one, later. See
    // stratum_fsm_set_accepted_share_hook().
    stratum_accepted_share_cb  on_accepted_share;
    void                       *on_accepted_share_ud;

    // Optional on-rejected-share hook (nullable, no-op when unset). See
    // stratum_fsm_set_rejected_share_hook().
    stratum_rejected_share_cb  on_rejected_share;
    void                       *on_rejected_share_ud;

    // Session/diagnostic counters -- also the backing store for the
    // producer snapshot (stratum_producer.c).
    uint32_t session_start_ms;
    uint32_t last_pool_job_ms;
    uint32_t last_share_ms;
    uint32_t last_tx_ms;
    uint32_t reconnect_count;
    uint32_t accepted;
    uint32_t rejected;
    uint32_t stale;
} stratum_fsm_ctx_t;

void stratum_fsm_init(stratum_fsm_ctx_t *ctx, const stratum_fsm_cfg_t *cfg);

// Perform one bounded slice of protocol work. Safe to call at a fixed
// cadence regardless of current state -- a no-op when there is nothing
// bounded to do yet (e.g. waiting on a timer).
void stratum_fsm_service(stratum_fsm_ctx_t *ctx, uint32_t now_ms);

// External reconnect request (e.g. WiFi IP loss). Consumed on the next
// stratum_fsm_service() call from any state.
void stratum_fsm_request_reconnect(stratum_fsm_ctx_t *ctx);

stratum_fsm_state_t stratum_fsm_state(const stratum_fsm_ctx_t *ctx);
bool                 stratum_fsm_is_connected(const stratum_fsm_ctx_t *ctx);

// One-shot: returns true (and clears the flag) the first call after a hard
// read/transport/handshake failure tore the session down; false otherwise
// (including after a clean/explicit reconnect). See hard_failure_pending's
// doc comment on stratum_fsm_ctx_t for the exact event set.
bool stratum_fsm_consumed_read_failure(stratum_fsm_ctx_t *ctx);

// Register (or clear, with cb=NULL) the optional on-accepted-share hook.
// Invoked synchronously from stratum_fsm_service() on an ACCEPTED
// mining.submit response only -- never on reject, never for any other
// request kind (keepalive/configure/subscribe/authorize/extranonce
// subscribe acks).
void stratum_fsm_set_accepted_share_hook(stratum_fsm_ctx_t *ctx, stratum_accepted_share_cb cb, void *ud);

// Register (or clear, with cb=NULL) the optional on-rejected-share hook.
// Invoked synchronously from stratum_fsm_service() on a REJECTED
// mining.submit response only -- same scoping as the accepted-share hook
// above.
void stratum_fsm_set_rejected_share_hook(stratum_fsm_ctx_t *ctx, stratum_rejected_share_cb cb, void *ud);
