#pragma once

#include <stdbool.h>
#include <stdint.h>

// In-flight JSON-RPC request id -> kind tracker.
//
// The pre-rebuild implementation matched an incoming response id against a
// SINGLE last-value field per kind (e.g. `s_state.keepalive_id`), never
// cleared after the ack arrived. Since every request (keepalive, submit,
// configure, ...) shares one monotonically-increasing id allocator, a
// keepalive ack that arrives AFTER a newer keepalive has already overwritten
// that single slot falls through to the generic "submit response" branch --
// inflating the accepted/rejected share counters with a keepalive ack. This
// is the known TM bug called out in TA-560's acceptance criteria.
//
// Fix: a small fixed-capacity ring of (id, kind) registrations. A response's
// id is looked up by MEMBERSHIP, not by last-value comparison, so it is
// routed correctly regardless of send/ack ordering. Entries are consumed
// (removed) on lookup -- one response per registered id.

typedef enum {
    STRATUM_REQID_NONE = 0,   // not registered / consumed already
    STRATUM_REQID_CONFIGURE,
    STRATUM_REQID_SUBSCRIBE,
    STRATUM_REQID_AUTHORIZE,
    STRATUM_REQID_KEEPALIVE,
    STRATUM_REQID_EXTRANONCE_SUBSCRIBE,
    STRATUM_REQID_SUBMIT,
} stratum_reqid_kind_t;

#define STRATUM_REQID_MAX_INFLIGHT 8

typedef struct {
    int                   id;
    stratum_reqid_kind_t  kind;
    bool                  used;
} stratum_reqid_slot_t;

typedef struct {
    stratum_reqid_slot_t slots[STRATUM_REQID_MAX_INFLIGHT];
} stratum_reqid_table_t;

void stratum_reqid_reset(stratum_reqid_table_t *t);

// Register `id` as kind. If the table is full, evicts the oldest slot
// (FIFO) rather than failing -- a full table means an unusually deep
// in-flight backlog; losing the oldest tracked id degrades gracefully to
// the pre-fix "unclassified" behavior for that one id, rather than
// blocking new registrations.
void stratum_reqid_register(stratum_reqid_table_t *t, int id, stratum_reqid_kind_t kind);

// Look up and CONSUME (remove) the registration for `id`. Returns
// STRATUM_REQID_NONE if `id` was never registered (or already consumed) --
// the caller's fallback path (e.g. treat as a plain submit response).
stratum_reqid_kind_t stratum_reqid_take(stratum_reqid_table_t *t, int id);
