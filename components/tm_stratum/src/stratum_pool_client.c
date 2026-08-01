// tm_stratum's V1 (line-protocol Stratum) implementation of the
// tm_pool_client_ops_t seam (tm_pool_client.h) -- a thin forwarding shim
// over the existing stratum_fsm_* API. See stratum_pool_client.h.
#include "stratum_pool_client.h"

#include <string.h>

static bb_err_t v1_init(void *ctx, const tm_pool_cfg_t *cfg, tm_pool_work_ops_t *work)
{
    if (!ctx || !cfg || !work) return BB_ERR_INVALID_ARG;
    stratum_pool_client_ctx_t *self = (stratum_pool_client_ctx_t *)ctx;

    // Per-instance production transport (TA-571 finding #4) -- bound to the
    // real bb_tcp_client on ESP_PLATFORM (see stratum_transport_esp_init()'s
    // own doc comment) and left zeroed (never connects) on host: host tests
    // exercise stratum_fsm_* directly against their own in-test transport
    // fake instead of through this adapter; this adapter only needs to LINK
    // on native (see platformio.ini's [env:native] build_src_filter), not
    // to actually connect.
    memset(&self->transport, 0, sizeof(self->transport));
#ifdef ESP_PLATFORM
    stratum_transport_esp_init(&self->transport, false /* tls */);
#endif

    // Borrowed pointers into *cfg (host/wallet/worker/pass) -- cfg must
    // outlive self, per tm_pool_client_ops_t.init()'s documented contract.
    stratum_fsm_cfg_t fsm_cfg = {
        .host                 = cfg->host,
        .port                 = cfg->port,
        .wallet               = cfg->wallet,
        .worker               = cfg->worker,
        .pass                 = cfg->pass,
        .extranonce_subscribe = cfg->extranonce_subscribe,
        .transport            = &self->transport,
        .work                 = work,
    };
    stratum_fsm_init(&self->fsm, &fsm_cfg);
    return BB_OK;
}

static void v1_service(void *ctx, uint32_t now_ms)
{
    stratum_fsm_service(&((stratum_pool_client_ctx_t *)ctx)->fsm, now_ms);
}

static bool v1_consumed_read_failure(void *ctx)
{
    return stratum_fsm_consumed_read_failure(&((stratum_pool_client_ctx_t *)ctx)->fsm);
}

static bool v1_is_connected(void *ctx)
{
    return stratum_fsm_is_connected(&((stratum_pool_client_ctx_t *)ctx)->fsm);
}

static void v1_request_reconnect(void *ctx)
{
    stratum_fsm_request_reconnect(&((stratum_pool_client_ctx_t *)ctx)->fsm);
}

static void v1_teardown(void *ctx)
{
    // No dynamic allocation to release -- just make sure the transport is
    // closed (idempotent per stratum_transport_ops_t.close()'s contract).
    stratum_pool_client_ctx_t *self = (stratum_pool_client_ctx_t *)ctx;
    if (self->fsm.cfg.transport && self->fsm.cfg.transport->close) {
        self->fsm.cfg.transport->close(self->fsm.cfg.transport->ctx);
    }
}

static const tm_pool_client_ops_t s_v1_ops = {
    .init                  = v1_init,
    .service               = v1_service,
    .consumed_read_failure = v1_consumed_read_failure,
    .is_connected          = v1_is_connected,
    .request_reconnect     = v1_request_reconnect,
    .teardown              = v1_teardown,
};

const tm_pool_client_ops_t *tm_stratum_v1_pool_client_ops(void)
{
    return &s_v1_ops;
}

void tm_stratum_set_accepted_share_hook(stratum_pool_client_ctx_t *ctx, stratum_accepted_share_cb cb, void *ud)
{
    stratum_fsm_set_accepted_share_hook(&ctx->fsm, cb, ud);
}
