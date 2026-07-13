#include "stratum_snap.h"
#include <string.h>
#include <stddef.h>

bb_err_t stratum_gather(void *vctx, void *vout)
{
    const stratum_fsm_ctx_t *ctx = (const stratum_fsm_ctx_t *)vctx;
    stratum_snap_t *out = (stratum_snap_t *)vout;
    if (!ctx || !out) {
        return BB_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    strncpy(out->pool_host, ctx->cfg.host ? ctx->cfg.host : "", sizeof(out->pool_host) - 1);
    out->pool_port       = ctx->cfg.port;
    out->connected       = ctx->connected;
    out->accepted        = ctx->accepted;
    out->rejected        = ctx->rejected;
    out->stale           = ctx->stale;
    out->difficulty      = ctx->proto.difficulty;
    out->last_job_ms     = ctx->last_pool_job_ms;
    out->reconnect_count = ctx->reconnect_count;
    out->fsm_state       = (int32_t)stratum_fsm_state(ctx);

    return BB_OK;
}

static const bb_serialize_field_t s_fields[] = {
    { .key = "pool_host",       .type = BB_TYPE_STR,  .offset = offsetof(stratum_snap_t, pool_host),
      .max_len = sizeof(((stratum_snap_t *)0)->pool_host) },
    { .key = "pool_port",       .type = BB_TYPE_U64,  .offset = offsetof(stratum_snap_t, pool_port) },
    { .key = "connected",       .type = BB_TYPE_BOOL, .offset = offsetof(stratum_snap_t, connected) },
    { .key = "accepted",        .type = BB_TYPE_U64,  .offset = offsetof(stratum_snap_t, accepted) },
    { .key = "rejected",        .type = BB_TYPE_U64,  .offset = offsetof(stratum_snap_t, rejected) },
    { .key = "stale",           .type = BB_TYPE_U64,  .offset = offsetof(stratum_snap_t, stale) },
    { .key = "difficulty",      .type = BB_TYPE_F64,  .offset = offsetof(stratum_snap_t, difficulty) },
    { .key = "last_job_ms",     .type = BB_TYPE_U64,  .offset = offsetof(stratum_snap_t, last_job_ms) },
    { .key = "reconnect_count", .type = BB_TYPE_U64,  .offset = offsetof(stratum_snap_t, reconnect_count) },
    { .key = "fsm_state",       .type = BB_TYPE_I64,  .offset = offsetof(stratum_snap_t, fsm_state) },
};

const bb_serialize_desc_t stratum_serialize_desc = {
    .type_name = "stratum",
    .fields = s_fields,
    .n_fields = sizeof(s_fields) / sizeof(s_fields[0]),
    .snap_size = sizeof(stratum_snap_t),
};
