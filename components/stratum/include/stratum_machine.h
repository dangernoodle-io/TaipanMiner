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
