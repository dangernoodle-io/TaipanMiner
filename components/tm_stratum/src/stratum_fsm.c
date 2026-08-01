#include "stratum_fsm.h"
#include "work_build.h"
#include "bb_serialize_json.h"

#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Timer helper -- bb_fsm stores only (event, duration_ms) per armed slot; we
// track "when armed" ourselves (keyed by event id) so a periodic
// stratum_fsm_service() call can decide whether a timer has expired without
// a real OS timer callback. See stratum_fsm.h's driving-contract comment.
// ---------------------------------------------------------------------------

static void fsm_arm(stratum_fsm_ctx_t *ctx, stratum_fsm_event_t ev, uint32_t ms)
{
    bb_fsm_arm_timer(&ctx->fsm, (bb_fsm_event_t)ev, ms);
    ctx->timer_armed_at_ms[ev] = ctx->now_ms;
}

static void fsm_service_timers(stratum_fsm_ctx_t *ctx)
{
    size_t n = bb_fsm_timer_count(&ctx->fsm);
    for (size_t i = 0; i < n; i++) {
        bb_fsm_event_t ev;
        uint32_t ms;
        if (!bb_fsm_timer_at(&ctx->fsm, i, &ev, &ms)) continue;
        uint32_t armed_at = ctx->timer_armed_at_ms[ev];
        if ((uint32_t)(ctx->now_ms - armed_at) >= ms) {
            bb_fsm_disarm_timer(&ctx->fsm, ev);
            bb_fsm_step(&ctx->fsm, ev, NULL);
            return;  // the armed set may have just changed under us
        }
    }
}

// Re-arms the keepalive-due timer on every FSM-initiated transport write
// (share submit, subscribe, authorize, ...), not just after sending a
// keepalive -- so keepalive is naturally suppressed whenever other traffic
// is already flowing, and only fires to fill a genuinely idle gap. A call
// before RUNNING is first entered is harmless: the timer isn't armed yet, so
// this simply updates last_tx_ms until on_enter_running() does its own
// initial fsm_arm() for all three RUNNING-state timers.
static void mark_tx(stratum_fsm_ctx_t *ctx)
{
    ctx->last_tx_ms = ctx->now_ms;
    if (bb_fsm_state(&ctx->fsm) == STRATUM_ST_RUNNING) {
        fsm_arm(ctx, STRATUM_EV_KEEPALIVE_DUE, STRATUM_WATCHDOG_KEEPALIVE_MS);
    }
}

// ---------------------------------------------------------------------------
// Single-funnel teardown (TA-530) -- the ONLY place that marks the session
// disconnected, closes the transport, resets session substate, resets the
// work-seam, and steps the reconnect backoff. Every failure row in the
// table (see the row list below) shares this exact action.
// ---------------------------------------------------------------------------

static void action_teardown(bb_fsm_t *fsm, void *vctx, bb_fsm_event_t event, void *evt_data)
{
    (void)fsm; (void)evt_data;
    stratum_fsm_ctx_t *ctx = (stratum_fsm_ctx_t *)vctx;

    // Hard read/transport/handshake failure parity with v1's
    // s_consecutive_fail_count: IO error, handshake reject/timeout, and
    // both drought watchdogs. Deliberately EXCLUDES STRATUM_EV_TCP_FAILED
    // (never established a session -- backoff already handles this on its
    // own) and STRATUM_EV_RECONNECT_REQUESTED (an explicit/clean disconnect,
    // e.g. WiFi IP loss, not an observed pool-side failure).
    switch ((stratum_fsm_event_t)event) {
    case STRATUM_EV_IO_ERROR:
    case STRATUM_EV_HANDSHAKE_REJECTED:
    case STRATUM_EV_HANDSHAKE_TIMEOUT:
    case STRATUM_EV_JOB_DROUGHT_TIMEOUT:
    case STRATUM_EV_SHARE_DROUGHT_TIMEOUT:
        ctx->hard_failure_pending = true;
        break;
    default:
        break;
    }

    ctx->cfg.transport->close(ctx->cfg.transport->ctx);
    if (ctx->cfg.work->reset) {
        ctx->cfg.work->reset(ctx->cfg.work->ctx);
    }

    ctx->connected = false;
    ctx->proto.extranonce1_len = 0;
    ctx->proto.version_mask = 0;
    memset(&ctx->proto.job, 0, sizeof(ctx->proto.job));
    stratum_reqid_reset(&ctx->reqids);

    ctx->session_start_ms = 0;
    ctx->last_pool_job_ms = 0;
    ctx->last_share_ms = 0;
    ctx->last_tx_ms = 0;
    ctx->reconnect_count++;

    stratum_backoff_step_t step = stratum_backoff_on_fail(&ctx->backoff);
    ctx->next_delay_ms = step.sleep_ms;
}

// ---------------------------------------------------------------------------
// on_entry hooks
// ---------------------------------------------------------------------------

static void on_enter_disconnected(bb_fsm_t *fsm, void *vctx, bb_fsm_state_t state)
{
    (void)fsm; (void)state;
    stratum_fsm_ctx_t *ctx = (stratum_fsm_ctx_t *)vctx;

    // bb_fsm_arm_timer(..., 0) DISARMS rather than firing immediately, so a
    // first-ever connect attempt (no prior failure to back off from) arms
    // for 1ms -- effectively "on the next service() tick" given the
    // shell's periodic cadence, without special-casing the service loop.
    uint32_t delay = ctx->first_attempt ? 1U : ctx->next_delay_ms;
    ctx->first_attempt = false;
    fsm_arm(ctx, STRATUM_EV_BACKOFF_ELAPSED, delay);
}

static void on_enter_configure(bb_fsm_t *fsm, void *vctx, bb_fsm_state_t state)
{
    (void)fsm; (void)state;
    stratum_fsm_ctx_t *ctx = (stratum_fsm_ctx_t *)vctx;

    ctx->proto.next_msg_id = 1;

    char params[256];
    if (stratum_machine_build_configure(params, sizeof(params)) < 0) {
        return;  // best-effort; CONFIGURE_DONE fires unconditionally from service()
    }
    char req[300];
    int id = ctx->proto.next_msg_id++;
    if (format_stratum_request(req, sizeof(req), id, "mining.configure", params) < 0) {
        return;
    }
    if (ctx->cfg.transport->write(ctx->cfg.transport->ctx, req)) {
        stratum_reqid_register(&ctx->reqids, id, STRATUM_REQID_CONFIGURE);
        mark_tx(ctx);
    }
}

static void on_enter_subscribe(bb_fsm_t *fsm, void *vctx, bb_fsm_state_t state)
{
    (void)fsm; (void)state;
    stratum_fsm_ctx_t *ctx = (stratum_fsm_ctx_t *)vctx;

    char params[64];
    char req[128];
    if (stratum_machine_build_subscribe(params, sizeof(params)) < 0) {
        return;
    }
    int id = ctx->proto.next_msg_id++;
    if (format_stratum_request(req, sizeof(req), id, "mining.subscribe", params) < 0) {
        return;
    }
    if (ctx->cfg.transport->write(ctx->cfg.transport->ctx, req)) {
        stratum_reqid_register(&ctx->reqids, id, STRATUM_REQID_SUBSCRIBE);
        mark_tx(ctx);
    }
    fsm_arm(ctx, STRATUM_EV_HANDSHAKE_TIMEOUT, STRATUM_HANDSHAKE_TIMEOUT_MS);
}

static void on_enter_authorize(bb_fsm_t *fsm, void *vctx, bb_fsm_state_t state)
{
    (void)fsm; (void)state;
    stratum_fsm_ctx_t *ctx = (stratum_fsm_ctx_t *)vctx;

    char params[256];
    char req[300];
    if (stratum_machine_build_authorize(params, sizeof(params),
                                        ctx->cfg.wallet, ctx->cfg.worker, ctx->cfg.pass) >= 0) {
        int id = ctx->proto.next_msg_id++;
        if (format_stratum_request(req, sizeof(req), id, "mining.authorize", params) >= 0 &&
            ctx->cfg.transport->write(ctx->cfg.transport->ctx, req)) {
            stratum_reqid_register(&ctx->reqids, id, STRATUM_REQID_AUTHORIZE);
            mark_tx(ctx);
        }
    }

    if (ctx->cfg.extranonce_subscribe) {
        int sub_id = ctx->proto.next_msg_id++;
        if (format_stratum_request(req, sizeof(req), sub_id, "mining.extranonce.subscribe", "[]") >= 0 &&
            ctx->cfg.transport->write(ctx->cfg.transport->ctx, req)) {
            stratum_reqid_register(&ctx->reqids, sub_id, STRATUM_REQID_EXTRANONCE_SUBSCRIBE);
            mark_tx(ctx);
        }
    }

    fsm_arm(ctx, STRATUM_EV_HANDSHAKE_TIMEOUT, STRATUM_HANDSHAKE_TIMEOUT_MS);
}

static void on_enter_running(bb_fsm_t *fsm, void *vctx, bb_fsm_state_t state)
{
    (void)fsm; (void)state;
    stratum_fsm_ctx_t *ctx = (stratum_fsm_ctx_t *)vctx;

    // TA-232: backoff resets on a SUCCESSFUL HANDSHAKE (authorized), not a
    // bare TCP connect.
    stratum_backoff_reset(&ctx->backoff);

    ctx->connected = true;
    ctx->session_start_ms = ctx->now_ms;
    ctx->last_pool_job_ms = ctx->now_ms;
    ctx->last_tx_ms = ctx->now_ms;

    fsm_arm(ctx, STRATUM_EV_JOB_DROUGHT_TIMEOUT, STRATUM_WATCHDOG_JOB_DROUGHT_MS);
    fsm_arm(ctx, STRATUM_EV_SHARE_DROUGHT_TIMEOUT, STRATUM_WATCHDOG_SHARE_DROUGHT_MS);
    fsm_arm(ctx, STRATUM_EV_KEEPALIVE_DUE, STRATUM_WATCHDOG_KEEPALIVE_MS);
}

// ---------------------------------------------------------------------------
// RUNNING-state self-loop actions
// ---------------------------------------------------------------------------

static void action_job_received(bb_fsm_t *fsm, void *vctx, bb_fsm_event_t event, void *evt_data)
{
    (void)fsm; (void)event; (void)evt_data;
    stratum_fsm_ctx_t *ctx = (stratum_fsm_ctx_t *)vctx;
    ctx->last_pool_job_ms = ctx->now_ms;
    fsm_arm(ctx, STRATUM_EV_JOB_DROUGHT_TIMEOUT, STRATUM_WATCHDOG_JOB_DROUGHT_MS);
}

static void action_share_submitted(bb_fsm_t *fsm, void *vctx, bb_fsm_event_t event, void *evt_data)
{
    (void)fsm; (void)event; (void)evt_data;
    stratum_fsm_ctx_t *ctx = (stratum_fsm_ctx_t *)vctx;
    ctx->last_share_ms = ctx->now_ms;
    fsm_arm(ctx, STRATUM_EV_SHARE_DROUGHT_TIMEOUT, STRATUM_WATCHDOG_SHARE_DROUGHT_MS);
    // The share submit itself is a transport write -- suppress a keepalive
    // that would otherwise fire right on top of it.
    mark_tx(ctx);
}

static void action_send_keepalive(bb_fsm_t *fsm, void *vctx, bb_fsm_event_t event, void *evt_data)
{
    (void)fsm; (void)event; (void)evt_data;
    stratum_fsm_ctx_t *ctx = (stratum_fsm_ctx_t *)vctx;

    char params[32];
    char req[96];
    if (stratum_machine_build_keepalive(params, sizeof(params), ctx->proto.difficulty) >= 0) {
        int id = ctx->proto.next_msg_id++;
        if (format_stratum_request(req, sizeof(req), id, "mining.suggest_difficulty", params) >= 0 &&
            ctx->cfg.transport->write(ctx->cfg.transport->ctx, req)) {
            stratum_reqid_register(&ctx->reqids, id, STRATUM_REQID_KEEPALIVE);
            mark_tx(ctx);
        }
    }
    // Unconditional fallback re-arm: even a failed/skipped send must not
    // leave the keepalive timer disarmed forever (mark_tx() above already
    // covers the success path; this catches the send-failed case too).
    fsm_arm(ctx, STRATUM_EV_KEEPALIVE_DUE, STRATUM_WATCHDOG_KEEPALIVE_MS);
}

// ---------------------------------------------------------------------------
// Table
// ---------------------------------------------------------------------------

static const bb_fsm_row_t s_rows[] = {
    { STRATUM_ST_DISCONNECTED, STRATUM_EV_BACKOFF_ELAPSED,     NULL, NULL,                    STRATUM_ST_CONNECTING },
    { STRATUM_ST_CONNECTING,   STRATUM_EV_TCP_CONNECTED,       NULL, NULL,                    STRATUM_ST_CONFIGURE },
    { STRATUM_ST_CONNECTING,   STRATUM_EV_TCP_FAILED,          NULL, action_teardown,         STRATUM_ST_DISCONNECTED },
    { STRATUM_ST_CONFIGURE,    STRATUM_EV_CONFIGURE_DONE,      NULL, NULL,                    STRATUM_ST_SUBSCRIBE },
    { STRATUM_ST_SUBSCRIBE,    STRATUM_EV_SUBSCRIBE_OK,        NULL, NULL,                    STRATUM_ST_AUTHORIZE },
    { STRATUM_ST_AUTHORIZE,    STRATUM_EV_AUTHORIZE_OK,        NULL, NULL,                    STRATUM_ST_RUNNING },
    { STRATUM_ST_RUNNING,      STRATUM_EV_JOB_RECEIVED,        NULL, action_job_received,     BB_FSM_STATE_SAME },
    { STRATUM_ST_RUNNING,      STRATUM_EV_SHARE_SUBMITTED,     NULL, action_share_submitted,  BB_FSM_STATE_SAME },
    { STRATUM_ST_RUNNING,      STRATUM_EV_KEEPALIVE_DUE,       NULL, action_send_keepalive,   BB_FSM_STATE_SAME },
    { BB_FSM_STATE_ANY,        STRATUM_EV_HANDSHAKE_REJECTED,  NULL, action_teardown,         STRATUM_ST_DISCONNECTED },
    { BB_FSM_STATE_ANY,        STRATUM_EV_HANDSHAKE_TIMEOUT,   NULL, action_teardown,         STRATUM_ST_DISCONNECTED },
    { BB_FSM_STATE_ANY,        STRATUM_EV_JOB_DROUGHT_TIMEOUT, NULL, action_teardown,         STRATUM_ST_DISCONNECTED },
    { BB_FSM_STATE_ANY,        STRATUM_EV_SHARE_DROUGHT_TIMEOUT, NULL, action_teardown,       STRATUM_ST_DISCONNECTED },
    { BB_FSM_STATE_ANY,        STRATUM_EV_IO_ERROR,            NULL, action_teardown,         STRATUM_ST_DISCONNECTED },
    { BB_FSM_STATE_ANY,        STRATUM_EV_RECONNECT_REQUESTED, NULL, action_teardown,         STRATUM_ST_DISCONNECTED },
};

static const bb_fsm_state_desc_t s_states[] = {
    { STRATUM_ST_DISCONNECTED, on_enter_disconnected, NULL },
    { STRATUM_ST_CONFIGURE,    on_enter_configure,     NULL },
    { STRATUM_ST_SUBSCRIBE,    on_enter_subscribe,     NULL },
    { STRATUM_ST_AUTHORIZE,    on_enter_authorize,     NULL },
    { STRATUM_ST_RUNNING,      on_enter_running,       NULL },
};

static const bb_fsm_desc_t s_desc = {
    .rows = s_rows,
    .row_count = sizeof(s_rows) / sizeof(s_rows[0]),
    .states = s_states,
    .state_count = sizeof(s_states) / sizeof(s_states[0]),
    .initial = STRATUM_ST_DISCONNECTED,
    .ctx = NULL,  // patched per-instance in stratum_fsm_init
};

// ---------------------------------------------------------------------------
// Pool message dispatch (service-loop driven, NOT a bb_fsm action -- safe to
// call bb_fsm_step from here)
// ---------------------------------------------------------------------------

static bool tok_method_is(const bb_serialize_json_tok_recorder_t *rec, bb_serialize_json_tok_idx_t method,
                          const char *name)
{
    const char *ptr;
    size_t len;
    if (!bb_serialize_json_tok_get_str(rec, method, &ptr, &len)) return false;
    size_t name_len = strlen(name);
    return len == name_len && memcmp(ptr, name, name_len) == 0;
}

// Scans a complete stratum line via the bb_serialize_json tok-recorder
// (BOUNDED mode -- the transport hands us a complete NUL-terminated line,
// see stratum_transport.h's read_line() contract). Sized to
// BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP (48), which comfortably covers
// mining.notify's 29-token worst case (16 merkle branches + clean_jobs) --
// see bb_serialize_json.h's Kconfig-bridge doc comment for the exact
// arithmetic. No arena: every stratum string value in scope (hex, job ids,
// method names) is escape-free, so bb_serialize_json_scan_bounded() always
// delivers them as a single direct CALLER_STABLE span of `line` -- see
// bb_serialize_json_tok_recorder_init()'s STRING VALUE STORAGE note.
static void process_line(stratum_fsm_ctx_t *ctx, const char *line)
{
    size_t line_len = strlen(line);

    bb_serialize_json_tok_t pool[BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP];
    bb_serialize_json_tok_recorder_t rec;
    if (bb_serialize_json_tok_recorder_init(&rec, line, line_len, pool,
                                            BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP, NULL, 0) != BB_OK) {
        return;
    }
    bb_serialize_json_ingest_t ingest = bb_serialize_json_tok_recorder_ingest(&rec);
    if (bb_serialize_json_scan_bounded(line, line_len, &ingest) != BB_OK) {
        return;  // malformed line -- ignore, matching the pre-rebuild parser's reject-and-skip behavior
    }

    bb_serialize_json_tok_idx_t root = bb_serialize_json_tok_root(&rec);
    if (root == BB_SERIALIZE_JSON_TOK_ABSENT || !bb_serialize_json_tok_is_obj(&rec, root)) {
        return;
    }

    bb_serialize_json_tok_idx_t method_tok = bb_serialize_json_tok_obj_get(&rec, root, "method", 6);
    bb_serialize_json_tok_idx_t id_tok     = bb_serialize_json_tok_obj_get(&rec, root, "id", 2);
    bb_serialize_json_tok_idx_t result_tok = bb_serialize_json_tok_obj_get(&rec, root, "result", 6);
    bb_serialize_json_tok_idx_t params_tok = bb_serialize_json_tok_obj_get(&rec, root, "params", 6);
    bb_serialize_json_tok_idx_t error_tok  = bb_serialize_json_tok_obj_get(&rec, root, "error", 5);

    if (method_tok != BB_SERIALIZE_JSON_TOK_ABSENT && bb_serialize_json_tok_is_str(&rec, method_tok)) {
        if (tok_method_is(&rec, method_tok, "mining.notify")) {
            if (stratum_machine_handle_notify(&ctx->proto, &rec, params_tok)) {
                mining_work_t work;
                if (build_work(&ctx->proto.job,
                               ctx->proto.extranonce1, ctx->proto.extranonce1_len,
                               ctx->proto.extranonce2, ctx->proto.extranonce2_size,
                               ctx->proto.version_mask, ctx->proto.difficulty,
                               ++ctx->work_seq, &work)
                    && ctx->cfg.work->publish) {
                    ctx->cfg.work->publish(ctx->cfg.work->ctx, &work);
                }
                bb_fsm_step(&ctx->fsm, STRATUM_EV_JOB_RECEIVED, NULL);
            }
        } else if (tok_method_is(&rec, method_tok, "mining.set_difficulty")) {
            if (stratum_machine_handle_set_difficulty(&ctx->proto, &rec, params_tok) &&
                ctx->proto.job.job_id[0] != '\0') {
                // A difficulty change invalidates the target baked into the
                // already-staged job -- rebuild work against the new
                // difficulty and republish it marked clean so the miner
                // stops hashing against the stale (too-easy or too-hard)
                // target immediately, rather than misjudging every share
                // until the next mining.notify.
                mining_work_t work;
                if (build_work(&ctx->proto.job,
                               ctx->proto.extranonce1, ctx->proto.extranonce1_len,
                               ctx->proto.extranonce2, ctx->proto.extranonce2_size,
                               ctx->proto.version_mask, ctx->proto.difficulty,
                               ++ctx->work_seq, &work)
                    && ctx->cfg.work->publish) {
                    work.clean = true;
                    ctx->cfg.work->publish(ctx->cfg.work->ctx, &work);
                }
            }
        } else if (tok_method_is(&rec, method_tok, "mining.set_extranonce")) {
            stratum_machine_handle_set_extranonce(&ctx->proto, &rec, params_tok);
        }
    } else if (id_tok != BB_SERIALIZE_JSON_TOK_ABSENT && bb_serialize_json_tok_is_num(&rec, id_tok)) {
        int64_t id64 = 0;
        bb_serialize_json_tok_get_i64(&rec, id_tok, &id64);
        int id = (int)id64;
        stratum_accepted_share_t share;
        stratum_reqid_kind_t kind = stratum_reqid_take(&ctx->reqids, id, &share);

        switch (kind) {
        case STRATUM_REQID_CONFIGURE:
            if (bb_serialize_json_tok_is_obj(&rec, result_tok)) {
                stratum_machine_handle_configure_result(&ctx->proto, &rec, result_tok);
            }
            break;
        case STRATUM_REQID_SUBSCRIBE:
            if (stratum_machine_handle_subscribe_result(&ctx->proto, &rec, result_tok)) {
                bb_fsm_step(&ctx->fsm, STRATUM_EV_SUBSCRIBE_OK, NULL);
            }
            // A malformed/absent result leaves the subscribe handshake
            // timer to fire (matches pre-rebuild "subscribe timeout" path).
            break;
        case STRATUM_REQID_AUTHORIZE: {
            bool authorized = false;
            if (bb_serialize_json_tok_get_bool(&rec, result_tok, &authorized) && authorized) {
                bb_fsm_step(&ctx->fsm, STRATUM_EV_AUTHORIZE_OK, NULL);
            } else {
                bb_fsm_step(&ctx->fsm, STRATUM_EV_HANDSHAKE_REJECTED, NULL);
            }
            break;
        }
        case STRATUM_REQID_KEEPALIVE:
        case STRATUM_REQID_EXTRANONCE_SUBSCRIBE:
            // Ack only -- no FSM event, no share counters touched. This is
            // exactly the id-kind tracking that fixes the pre-rebuild bug
            // where a delayed keepalive ack could fall through to the
            // generic submit-response branch and inflate reject/accept
            // counters (see stratum_reqid.h).
            break;
        case STRATUM_REQID_SUBMIT: {
            // `share` was already filled by stratum_reqid_take() above (its
            // kind resolved to SUBMIT) -- the slot is reclaimed either way
            // (accept or reject) since take() always consumes it.
            if (error_tok != BB_SERIALIZE_JSON_TOK_ABSENT && !bb_serialize_json_tok_is_null(&rec, error_tok)) {
                ctx->rejected++;
                int code = stratum_parse_error_code(&rec, error_tok);
                if (stratum_machine_classify_reject(code) == STRATUM_REJECT_STALE_PREVHASH) {
                    ctx->stale++;
                }
                // Reject: the hook is NEVER invoked.
            } else {
                bool accepted = false;
                if (bb_serialize_json_tok_get_bool(&rec, result_tok, &accepted) && accepted) {
                    ctx->accepted++;
                    if (ctx->on_accepted_share) {
                        ctx->on_accepted_share(ctx->on_accepted_share_ud, &share);
                    }
                }
            }
            break;
        }
        case STRATUM_REQID_NONE:
        default: {
            if (error_tok != BB_SERIALIZE_JSON_TOK_ABSENT && !bb_serialize_json_tok_is_null(&rec, error_tok)) {
                ctx->rejected++;
                int code = stratum_parse_error_code(&rec, error_tok);
                if (stratum_machine_classify_reject(code) == STRATUM_REJECT_STALE_PREVHASH) {
                    ctx->stale++;
                }
            } else {
                bool accepted = false;
                if (bb_serialize_json_tok_get_bool(&rec, result_tok, &accepted) && accepted) {
                    ctx->accepted++;
                }
            }
            break;
        }
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void stratum_fsm_init(stratum_fsm_ctx_t *ctx, const stratum_fsm_cfg_t *cfg)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->cfg = *cfg;
    ctx->proto.next_msg_id = 1;
    ctx->proto.extranonce2_size = 4;
    ctx->proto.difficulty = 512.0;
    stratum_backoff_init(&ctx->backoff);
    stratum_reqid_reset(&ctx->reqids);
    ctx->first_attempt = true;

    // bb_fsm stores `desc` as a pointer -- each ctx instance needs its OWN
    // copy (not a shared static) so multiple concurrent instances (e.g. two
    // host tests) never alias each other's `.ctx`.
    ctx->fsm_desc = s_desc;
    ctx->fsm_desc.ctx = ctx;
    bb_fsm_init(&ctx->fsm, &ctx->fsm_desc);
}

void stratum_fsm_service(stratum_fsm_ctx_t *ctx, uint32_t now_ms)
{
    ctx->now_ms = now_ms;

    fsm_service_timers(ctx);

    if (ctx->reconnect_requested) {
        ctx->reconnect_requested = false;
        bb_fsm_step(&ctx->fsm, STRATUM_EV_RECONNECT_REQUESTED, NULL);
        return;
    }

    switch (bb_fsm_state(&ctx->fsm)) {
    case STRATUM_ST_CONNECTING: {
        bool ok = ctx->cfg.transport->connect(ctx->cfg.transport->ctx, ctx->cfg.host, ctx->cfg.port);
        bb_fsm_step(&ctx->fsm, ok ? STRATUM_EV_TCP_CONNECTED : STRATUM_EV_TCP_FAILED, NULL);
        break;
    }
    case STRATUM_ST_CONFIGURE:
        // Fire-and-forget: the pre-rebuild client never blocked on a
        // mining.configure response either -- it sends, moves straight to
        // subscribe, and handles the (possibly late) response generically
        // via the id-kind dispatch in process_line().
        bb_fsm_step(&ctx->fsm, STRATUM_EV_CONFIGURE_DONE, NULL);
        break;
    case STRATUM_ST_SUBSCRIBE:
    case STRATUM_ST_AUTHORIZE:
    case STRATUM_ST_RUNNING: {
        char line[4096];
        stratum_io_result_t r = ctx->cfg.transport->read_line(ctx->cfg.transport->ctx, line, sizeof(line), 50);
        if (r == STRATUM_IO_ERROR) {
            bb_fsm_step(&ctx->fsm, STRATUM_EV_IO_ERROR, NULL);
            break;
        }
        if (r == STRATUM_IO_OK) {
            process_line(ctx, line);
        }

        if (bb_fsm_state(&ctx->fsm) == STRATUM_ST_RUNNING) {
            mining_result_t res;
            if (ctx->cfg.work->drain && ctx->cfg.work->drain(ctx->cfg.work->ctx, &res)) {
                char params[256];
                char req[300];
                if (format_submit_params(params, sizeof(params),
                                         ctx->cfg.wallet, ctx->cfg.worker,
                                         res.job_id, res.extranonce2_hex,
                                         res.ntime_hex, res.nonce_hex,
                                         res.version_hex) >= 0) {
                    int id = ctx->proto.next_msg_id++;
                    if (format_stratum_request(req, sizeof(req), id, "mining.submit", params) >= 0) {
                        if (ctx->cfg.transport->write(ctx->cfg.transport->ctx, req)) {
                            // Register the id AND its full share payload
                            // atomically, in the SAME reqid slot -- see
                            // stratum_reqid.h's doc comment (TA-571 finding
                            // #2: a second, independently-evicted table used
                            // to carry this and could desync under churn).
                            // tm_stratum never records/delivers this itself;
                            // it only offers it up via the optional hook on
                            // an ACCEPT (process_line()'s STRATUM_REQID_SUBMIT
                            // dispatch).
                            stratum_accepted_share_t share;
                            memset(&share, 0, sizeof(share));
                            strncpy(share.job_id, res.job_id, sizeof(share.job_id) - 1);
                            strncpy(share.extranonce2_hex, res.extranonce2_hex, sizeof(share.extranonce2_hex) - 1);
                            share.extranonce2_len = (uint8_t)strlen(share.extranonce2_hex);
                            strncpy(share.ntime_hex, res.ntime_hex, sizeof(share.ntime_hex) - 1);
                            strncpy(share.nonce_hex, res.nonce_hex, sizeof(share.nonce_hex) - 1);
                            share.version_rolled = (res.version_hex[0] != '\0');
                            if (share.version_rolled) {
                                share.version_bits = (uint32_t)strtoul(res.version_hex, NULL, 16);
                            }
                            share.diff = res.share_diff;
                            memcpy(share.hash_prefix, res.hash_prefix, sizeof(share.hash_prefix));
                            stratum_reqid_register_submit(&ctx->reqids, id, &share);

                            bb_fsm_step(&ctx->fsm, STRATUM_EV_SHARE_SUBMITTED, NULL);
                        } else {
                            bb_fsm_step(&ctx->fsm, STRATUM_EV_IO_ERROR, NULL);
                        }
                    }
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

void stratum_fsm_request_reconnect(stratum_fsm_ctx_t *ctx)
{
    ctx->reconnect_requested = true;
}

stratum_fsm_state_t stratum_fsm_state(const stratum_fsm_ctx_t *ctx)
{
    return (stratum_fsm_state_t)bb_fsm_state(&ctx->fsm);
}

bool stratum_fsm_is_connected(const stratum_fsm_ctx_t *ctx)
{
    return ctx->connected;
}

bool stratum_fsm_consumed_read_failure(stratum_fsm_ctx_t *ctx)
{
    bool v = ctx->hard_failure_pending;
    ctx->hard_failure_pending = false;
    return v;
}

void stratum_fsm_set_accepted_share_hook(stratum_fsm_ctx_t *ctx, stratum_accepted_share_cb cb, void *ud)
{
    ctx->on_accepted_share = cb;
    ctx->on_accepted_share_ud = ud;
}
