#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bb_fsm.h"
#include "stratum_machine.h"
#include "stratum_backoff.h"
#include "stratum_reqid.h"
#include "stratum_transport.h"
#include "stratum_work_seam.h"

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

typedef struct {
    const char *host;
    uint16_t    port;
    const char *wallet;
    const char *worker;
    const char *pass;
    bool        extranonce_subscribe;  // send mining.extranonce.subscribe (TA-306)

    stratum_transport_ops_t *transport;  // borrowed, must outlive the ctx
    stratum_work_ops_t      *work;       // borrowed, must outlive the ctx
} stratum_fsm_cfg_t;

typedef struct {
    bb_fsm_t fsm;
    bb_fsm_desc_t fsm_desc;   // per-instance copy of the static table desc, with .ctx patched to `this`

    stratum_fsm_cfg_t cfg;

    stratum_state_t         proto;    // pool session state (stratum_machine.h)
    stratum_backoff_t       backoff;
    stratum_reqid_table_t   reqids;
    uint32_t                next_delay_ms;   // computed by action_teardown, consumed by on_enter_disconnected
    bool                    first_attempt;
    uint32_t                work_seq;        // monotonic build_work() counter, never reset by teardown

    bool     connected;
    bool     reconnect_requested;
    uint32_t now_ms;                          // stashed by stratum_fsm_service() for hooks/actions to read
    uint32_t timer_armed_at_ms[STRATUM_EV_COUNT];

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
