#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "work.h"

// Stratum protocol state — all session data the machine needs to step.
// Phase 1 of TA-273: this struct collects fields that today live as
// file-statics in stratum.c. Behavior unchanged; phases 2-3 add the
// step function and host tests.
//
// IO-shell concerns (socket, line buffer, watchdog ticks, reconnect
// backoff, volatile connect/reconnect flags, callback pointers) stay
// in stratum.c and are NOT part of this struct.
typedef struct {
    int      next_msg_id;            // JSON-RPC id allocator (init: 1)

    // Pool-assigned identity from mining.subscribe result
    char     extranonce1_hex[32];
    uint8_t  extranonce1[MAX_EXTRANONCE1_SIZE];
    size_t   extranonce1_len;
    int      extranonce2_size;       // bytes (init: 4)

    // Pool-driven session parameters
    double   difficulty;             // pool difficulty (init: 512.0)
    uint32_t version_mask;           // BIP 320 mask, 0 if not configured

    // In-flight JSON-RPC request ids (0 = nothing in flight)
    int      configure_id;
    int      subscribe_id;
    int      authorize_id;
    int      keepalive_id;

    // Most recent job from mining.notify
    stratum_job_t job;

    // Work-stream counters
    uint32_t extranonce2;            // rolling extranonce2 counter
    uint32_t work_seq;               // monotonic work sequence
} stratum_state_t;

// JSON-RPC builders for Stratum protocol requests.
// Each returns chars written (excluding null terminator) on success, -1 on truncation.

// Build mining.configure request params (version rolling support).
// Output: [["version-rolling"],{"version-rolling.mask":"1fffe000","version-rolling.min-bit-count":13}]
int stratum_machine_build_configure(char *buf, size_t n);

// Build mining.subscribe request params.
// Output: ["TaipanMiner/0.1"]
int stratum_machine_build_subscribe(char *buf, size_t n);

// Build mining.authorize request params.
// Output: ["<wallet>.<worker>","<pass>"]
int stratum_machine_build_authorize(char *buf, size_t n,
                                    const char *wallet, const char *worker,
                                    const char *pass);

// Build mining.suggest_difficulty request params (used as app-level keepalive).
// Output: [<difficulty with %.4f format>]
int stratum_machine_build_keepalive(char *buf, size_t n, double difficulty);

// ---------------------------------------------------------------------------
// Response handlers — pure state mutators.
// Each receives an already-parsed bb_json_t item (caller owns/frees it).
// Handlers MUST NOT retain pointers into the JSON tree across calls.
// Returns true on success, false on parse/validation failure.
// ---------------------------------------------------------------------------
#include "bb_json.h"

// Handle mining.configure result: parse version-rolling.mask into st->version_mask.
// Pool may omit version-rolling → mask stays 0 (non-fatal; caller logs).
bool stratum_machine_handle_configure_result(stratum_state_t *st, bb_json_t result);

// Handle mining.subscribe result: parse extranonce1 (index 1) and extranonce2_size (index 2).
// Validates extranonce1 hex length fits MAX_EXTRANONCE1_SIZE.
bool stratum_machine_handle_subscribe_result(stratum_state_t *st, bb_json_t result);

// Handle mining.set_difficulty params: bounds-check and set st->difficulty.
// Rejects diff <= 0, NaN, and infinity.
bool stratum_machine_handle_set_difficulty(stratum_state_t *st, bb_json_t params);

// Handle mining.notify params: parse all fields into st->job.
bool stratum_machine_handle_notify(stratum_state_t *st, bb_json_t params);

// ---------------------------------------------------------------------------
// Work builder — pure math, reads state, writes out.
// Caller is responsible for incrementing st->extranonce2 before/after.
// Increments st->work_seq on success.
// ---------------------------------------------------------------------------
bool stratum_machine_build_work(stratum_state_t *st, mining_work_t *out);
