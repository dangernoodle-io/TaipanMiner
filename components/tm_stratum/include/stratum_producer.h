#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bb_core.h"
#include "bb_serialize.h"
#include "stratum_fsm.h"

// Producer-shape surface (TA-560's non-negotiable delivery seam): a POD
// snapshot struct, a pure gather function, and a static bb_serialize
// descriptor -- format-neutral, no I/O, no allocation, no knowledge of any
// specific delivery mechanism. Registration with whatever telemetry
// registry consumes this descriptor happens in the COMPOSITION ROOT, never
// here -- this component is left unbound (no telemetry registry composed
// yet), same as tm_mining's own producer.
//
// Every numeric field is widened to uint64_t/int64_t/double: bb_serialize's
// walker always reads exactly sizeof(that C type) at the descriptor's
// declared offset per its bb_type_t (BB_TYPE_U64/BB_TYPE_I64 == 8 bytes,
// always) -- a narrower field described with a wider type is a silent OOB
// read. (Fixed from the pre-rebuild stratum_snap_t, which declared
// pool_port -- a uint16_t -- as BB_TYPE_U64, an 8-byte read over a 2-byte
// field.)

typedef struct {
    char     pool_host[96];
    uint64_t pool_port;
    bool     connected;
    uint64_t accepted;
    uint64_t rejected;
    uint64_t stale;
    double   difficulty;
    uint64_t last_job_ms;
    uint64_t reconnect_count;
    int64_t  fsm_state;   // stratum_fsm_state_t
} stratum_snap_t;

// Pure: copies ctx's current fields into *out (a stratum_snap_t). No I/O,
// no allocation, no locks taken here -- if a caller shares ctx across
// tasks, it must already hold whatever lock guards ctx before calling this
// (stratum_fsm_ctx_t itself is single-writer/not internally locked,
// matching bb_fsm's own contract). void* signature matches a gather_fn's
// shape so a future registration in the composition root can pass this
// function pointer directly, modulo argument-order adaptation at the
// registration call site, not here.
bb_err_t stratum_gather(void *ctx, void *out);

extern const bb_serialize_desc_t stratum_serialize_desc;
