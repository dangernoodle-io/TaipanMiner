#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "stratum_job.h"
#include "bb_serialize_json.h"

// Stratum protocol state -- all session data the pure machine needs to
// build requests and parse responses. Ported from the pre-rebuild
// stratum_machine.h (TA-273); TRIMMED of stratum_machine_build_work() and
// its mining_work_t/coinbase dependency -- that lives in work_build.h's
// build_work() now (tm_stratum owns both the protocol parse AND the
// coinbase/header build, composed by stratum_fsm.c on a received job).
typedef struct {
    int      next_msg_id;            // JSON-RPC id allocator (init: 1)

    // Pool-assigned identity from mining.subscribe result
    char     extranonce1_hex[32];
    uint8_t  extranonce1[STRATUM_MAX_EXTRANONCE1_SIZE];
    size_t   extranonce1_len;
    int      extranonce2_size;       // bytes (init: 4)

    // Pool-driven session parameters
    double   difficulty;             // pool difficulty (init: 512.0)
    uint32_t version_mask;           // BIP 320 mask, 0 if not configured

    // Most recent job from mining.notify
    stratum_job_t job;

    // Work-stream counters
    uint32_t extranonce2;            // rolling extranonce2 counter
} stratum_state_t;

// JSON-RPC builders for Stratum protocol requests.
// Each returns chars written (excluding null terminator) on success, -1 on truncation.

// Build mining.configure request params (version rolling support).
int stratum_machine_build_configure(char *buf, size_t n);

// Build mining.subscribe request params.
int stratum_machine_build_subscribe(char *buf, size_t n);

// Build mining.authorize request params.
int stratum_machine_build_authorize(char *buf, size_t n,
                                    const char *wallet, const char *worker,
                                    const char *pass);

// Build mining.suggest_difficulty request params (used as app-level keepalive).
int stratum_machine_build_keepalive(char *buf, size_t n, double difficulty);

// Format mining.submit params into buf. Returns number of chars written
// (excluding null), or -1 on truncation. When version_hex is NULL or empty
// string -> 5-field format (no version). Otherwise 6-field format.
int format_submit_params(char *buf, size_t buf_size,
                         const char *wallet_addr, const char *worker_name,
                         const char *job_id, const char *extranonce2_hex,
                         const char *ntime_hex, const char *nonce_hex,
                         const char *version_hex);

// Format a JSON-RPC request into buf. Returns chars written (excl null), or -1 on truncation.
int format_stratum_request(char *buf, size_t buf_size,
                           int id, const char *method, const char *params_json);

// ---------------------------------------------------------------------------
// Response handlers -- pure state mutators, driven off an already-scanned
// bb_serialize_json_tok_recorder_t (see bb_serialize_json.h). `item` names
// the token (object or array) each handler navigates from -- the caller
// (stratum_fsm.c's process_line()) locates it via tok_obj_get/tok_arr_at on
// the parsed line's root object. Handlers MUST NOT retain rec/item beyond
// the call (the recorder's pool/arena are the caller's stack-scoped scratch).
// Returns true on success, false on parse/validation failure.
// ---------------------------------------------------------------------------

bool stratum_machine_handle_configure_result(stratum_state_t *st,
                                             const bb_serialize_json_tok_recorder_t *rec,
                                             bb_serialize_json_tok_idx_t result);
bool stratum_machine_handle_subscribe_result(stratum_state_t *st,
                                             const bb_serialize_json_tok_recorder_t *rec,
                                             bb_serialize_json_tok_idx_t result);
bool stratum_machine_handle_set_difficulty(stratum_state_t *st,
                                           const bb_serialize_json_tok_recorder_t *rec,
                                           bb_serialize_json_tok_idx_t params);
bool stratum_machine_handle_notify(stratum_state_t *st,
                                   const bb_serialize_json_tok_recorder_t *rec,
                                   bb_serialize_json_tok_idx_t params);
bool stratum_machine_handle_set_extranonce(stratum_state_t *st,
                                           const bb_serialize_json_tok_recorder_t *rec,
                                           bb_serialize_json_tok_idx_t params);

// Parse stratum error code from a JSON-RPC error token: handles both the
// [code, "message", "data"] array form and the {"code": N, ...} object form.
// Returns the error code, or -1 if absent/unparseable.
int stratum_parse_error_code(const bb_serialize_json_tok_recorder_t *rec,
                             bb_serialize_json_tok_idx_t error_item);

// ---------------------------------------------------------------------------
// Reject categorization -- pure function for testability.
// ---------------------------------------------------------------------------
typedef enum {
    STRATUM_REJECT_JOB_NOT_FOUND  = 21,
    STRATUM_REJECT_DUPLICATE      = 22,
    STRATUM_REJECT_LOW_DIFFICULTY = 23,
    STRATUM_REJECT_STALE_PREVHASH = 25,
    STRATUM_REJECT_OTHER          = -1,  // not a real code; sentinel
} stratum_reject_kind_t;

stratum_reject_kind_t stratum_machine_classify_reject(int code);

// ---------------------------------------------------------------------------
// WiFi zombie-kick decision -- pure helper, host-testable (TA-440).
// ---------------------------------------------------------------------------
#define STRATUM_WIFI_KICK_THRESHOLD 5

bool stratum_should_kick_wifi(int fail_count, bool has_ip);
