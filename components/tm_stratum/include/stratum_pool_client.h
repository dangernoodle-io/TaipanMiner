#pragma once

#include "stratum_fsm.h"
#include "tm_pool_client.h"

// tm_stratum's V1 (line-protocol Stratum) implementation of the
// tm_pool_client_ops_t seam (tm_pool_client.h). See stratum_pool_client.c.
//
// `ctx` for every ops entry point is a caller-allocated
// stratum_pool_client_ctx_t* (below) -- a plain wrapper pairing the FSM's
// own ctx with its per-instance production transport. Earlier this adapter
// kept its transport in a file-scope static, which silently assumed at most
// one active pool client ever exists process-wide (TA-571 firmware review
// finding #4); the real model IS one active pool client at a time (a future
// policy engine switches which pool by re-init'ing, not by running two
// concurrently), but a per-instance ctx removes the assumption instead of
// just asserting it, and costs nothing extra to plumb. Callers pass the
// SAME pointer to every ops call and to tm_stratum_set_accepted_share_hook()
// below.
typedef struct {
    stratum_fsm_ctx_t        fsm;
    stratum_transport_ops_t  transport;  // bound to bb_tcp_client on ESP_PLATFORM, unused stub on host
} stratum_pool_client_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

const tm_pool_client_ops_t *tm_stratum_v1_pool_client_ops(void);

// Composition-facing alias for stratum_fsm_set_accepted_share_hook() --
// takes the same stratum_pool_client_ctx_t* used with
// tm_stratum_v1_pool_client_ops(). The hook is optional and unset by
// default (a no-op) -- tm_stratum never depends on tm_pool_stats or any
// other delivery/recording sink; a later composition-root PR binds `cb`/
// `ud` to one.
void tm_stratum_set_accepted_share_hook(stratum_pool_client_ctx_t *ctx, stratum_accepted_share_cb cb, void *ud);

// Composition-facing alias for stratum_fsm_set_rejected_share_hook() --
// same scoping/defaults as tm_stratum_set_accepted_share_hook() above.
void tm_stratum_set_rejected_share_hook(stratum_pool_client_ctx_t *ctx, stratum_rejected_share_cb cb, void *ud);

#ifdef __cplusplus
}
#endif
