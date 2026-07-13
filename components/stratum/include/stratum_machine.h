#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "stratum_job.h"

// Stratum protocol state -- all session data the pure machine needs to
// build requests and parse responses. Ported from the pre-rebuild
// stratum_machine.h (TA-273); TRIMMED of stratum_machine_build_work() and
// its mining_work_t/coinbase dependency -- mining doesn't exist in the v2
// floor yet (see stratum_job.h). That builder returns when mining's own
// work component is ported; until then, a completed job crosses the
// stratum_work_publish() seam as a raw stratum_job_t.
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

// ---------------------------------------------------------------------------
// Response handlers -- pure state mutators.
// Each receives an already-parsed bb_json_t item (caller owns/frees it).
// Handlers MUST NOT retain pointers into the JSON tree across calls.
// Returns true on success, false on parse/validation failure.
// ---------------------------------------------------------------------------
#include "bb_json.h"

bool stratum_machine_handle_configure_result(stratum_state_t *st, bb_json_t result);
bool stratum_machine_handle_subscribe_result(stratum_state_t *st, bb_json_t result);
bool stratum_machine_handle_set_difficulty(stratum_state_t *st, bb_json_t params);
bool stratum_machine_handle_notify(stratum_state_t *st, bb_json_t params);
bool stratum_machine_handle_set_extranonce(stratum_state_t *st, bb_json_t params);

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
