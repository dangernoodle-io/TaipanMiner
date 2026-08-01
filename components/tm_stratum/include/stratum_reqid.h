#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stratum_share.h"  // stratum_accepted_share_t (STRATUM_REQID_SUBMIT slot payload only)

// In-flight JSON-RPC request id -> kind (+ optional SUBMIT share payload)
// tracker.
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
//
// A STRATUM_REQID_SUBMIT registration also carries the full
// stratum_accepted_share_t payload captured at submit time (see
// stratum_reqid_register_submit()), IN THE SAME SLOT as its id/kind. An
// earlier revision tracked share payloads in a second, independently-
// evicted table keyed the same way; under keepalive/extranonce_subscribe
// churn the two tables could desync (the id evicted from this table while
// its share record lived on, orphaned, in the other), silently dropping an
// accepted share's on-accepted-share hook invocation (TA-571 firmware
// review finding #2). Folding the payload into this table's own slot makes
// that desync structurally impossible.

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
    int                        id;
    stratum_reqid_kind_t       kind;
    bool                       used;
    stratum_accepted_share_t   share;  // valid only when kind == STRATUM_REQID_SUBMIT
} stratum_reqid_slot_t;

typedef struct {
    stratum_reqid_slot_t slots[STRATUM_REQID_MAX_INFLIGHT];
} stratum_reqid_table_t;

void stratum_reqid_reset(stratum_reqid_table_t *t);

// Register `id` as kind, with no share payload (every kind except SUBMIT --
// use stratum_reqid_register_submit() for SUBMIT). If the table is full,
// evicts the oldest slot (FIFO) rather than failing -- a full table means an
// unusually deep in-flight backlog; losing the oldest tracked id degrades
// gracefully to the pre-fix "unclassified" behavior for that one id, rather
// than blocking new registrations.
void stratum_reqid_register(stratum_reqid_table_t *t, int id, stratum_reqid_kind_t kind);

// Register a SUBMIT id together with its full share payload, atomically, in
// ONE slot/eviction operation -- see this header's own doc comment for why
// that atomicity is the point (TA-571 finding #2). Same FIFO eviction
// discipline as stratum_reqid_register().
void stratum_reqid_register_submit(stratum_reqid_table_t *t, int id, const stratum_accepted_share_t *rec);

// Look up and CONSUME (remove) the registration for `id`. Returns
// STRATUM_REQID_NONE if `id` was never registered (or already consumed) --
// the caller's fallback path (e.g. treat as a plain submit response). If
// the registration's kind is STRATUM_REQID_SUBMIT and `share_out` is
// non-NULL, *share_out is filled with the captured record; share_out is
// ignored (left untouched) for every other kind.
stratum_reqid_kind_t stratum_reqid_take(stratum_reqid_table_t *t, int id, stratum_accepted_share_t *share_out);
