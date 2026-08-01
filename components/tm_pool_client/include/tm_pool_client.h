#pragma once

#include "bb_core.h"
#include "tm_pool_cfg.h"          // tm_pool_cfg_t (tm_pool_config, PR1)
#include "tm_pool_work_seam.h"    // tm_pool_work_ops_t (this component's own seam)

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// tm_pool_client -- the pool-client seam (TM v2 pool subsystem PR4, TA-571).
// Header-only interface: the thinnest vtable a future policy engine needs to
// drive ANY pool-connection backend without caring how it talks to the pool.
// tm_stratum's V1 adapter (stratum_pool_client.c) is the first and only
// implementation today; a later Stratum V2 implementation binds against the
// exact same ops table.
//
// Deliberately protocol-agnostic: no line-protocol I/O method lives here
// (SV2 bypasses line-protocol entirely -- see stratum_transport.h's own
// doc comment for why that seam stays INSIDE tm_stratum, one layer down).
// Do not grow this table with anything more specific than "drive one
// bounded slice of work" / "am I connected" / "make me reconnect" /
// "release me" -- that keeps every future backend implementable without an
// ops-table break.
//
// Ownership: `ctx` is caller-allocated and caller-owned (typically a
// concrete per-backend context struct, e.g. a stratum_pool_client_ctx_t for
// the V1 adapter) -- this seam neither allocates nor frees it. `cfg` passed
// to init() must outlive the ctx: several backends (the V1 adapter
// included) borrow string pointers out of it rather than copying.
typedef struct {
    // One-time bring-up: bind `work` (the cross-core work/result seam,
    // tm_pool_work_seam.h -- this component's own) and any config drawn
    // from `cfg`. Returns BB_OK on success.
    bb_err_t (*init)(void *ctx, const tm_pool_cfg_t *cfg, tm_pool_work_ops_t *work);

    // Perform one bounded slice of connection-management work. Safe to call
    // at a fixed cadence regardless of connection state -- implementations
    // must never block past their own bounded I/O (same contract as
    // stratum_fsm_service()).
    void (*service)(void *ctx, uint32_t now_ms);

    // One-shot: true the first call after a hard read/transport/handshake
    // failure has been observed since the last call, false otherwise
    // (including after a clean/explicit reconnect). Lets a policy engine
    // count "3 consecutive hard failures" without duplicating per-backend
    // failure classification.
    bool (*consumed_read_failure)(void *ctx);

    bool (*is_connected)(void *ctx);

    // External reconnect request (e.g. WiFi IP loss). Consumed on the next
    // service() call.
    void (*request_reconnect)(void *ctx);

    // Release any held resources (e.g. close the transport). Idempotent;
    // safe to call when already disconnected/uninitialized.
    void (*teardown)(void *ctx);
} tm_pool_client_ops_t;

#ifdef __cplusplus
}
#endif
