#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bb_core.h"
#include "bb_serialize.h"
#include "stratum_fsm.h"

// Producer-shape surface (TA-560's non-negotiable delivery seam): a POD
// snapshot struct, a pure gather function, and a static bb_serialize
// descriptor -- format-neutral, no I/O, no allocation, no knowledge of
// delivery (bb_pub/bb_cache/http/mqtt/...). Registration with whatever
// telemetry registry consumes this descriptor happens in the COMPOSITION
// ROOT (src/main.c's PRODUCER REGISTRATION SLOT), never here -- see that
// file's comment for the current state of that wiring.

typedef struct {
    char     pool_host[96];
    uint16_t pool_port;
    bool     connected;
    uint32_t accepted;
    uint32_t rejected;
    uint32_t stale;
    double   difficulty;
    uint32_t last_job_ms;
    uint32_t reconnect_count;
    int32_t  fsm_state;   // stratum_fsm_state_t
} stratum_snap_t;

// Pure: copies ctx's current fields into *out (a stratum_snap_t). No I/O,
// no allocation, no locks taken here -- if a caller shares ctx across
// tasks, it must already hold whatever lock guards ctx before calling this
// (stratum_fsm_ctx_t itself is single-writer/not internally locked,
// matching bb_fsm's own contract). void* signature matches
// bb_cache_gather_fn's shape (see bb_cache_serialize.h) so a future
// registration in main.c can pass this function pointer directly, modulo
// bb_cache_gather_fn's (dst, ctx) argument order -- a one-line adapter at
// the registration call site, not here.
bb_err_t stratum_gather(void *ctx, void *out);

extern const bb_serialize_desc_t stratum_serialize_desc;
