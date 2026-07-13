// Production stratum_transport_ops_t backend -- a straight pass-through to
// bb_tcp_client (the mandated dumb transport; see stratum_transport.h).
// Compiled only under ESP_PLATFORM; an empty translation unit on host so the
// native test env can list this file unconditionally alongside the rest of
// components/stratum/src without special-casing it out.
#ifdef ESP_PLATFORM

#include "stratum_transport.h"
#include "bb_tcp_client.h"
#include <string.h>

// Matches the largest stratum message observed in the pre-rebuild
// implementation (mining.notify with deep merkle branches).
#define STRATUM_TRANSPORT_LINEBUF_SIZE 4096

typedef struct {
    bb_tcp_client_t handle;
    bool            tls;
    char            linebuf[STRATUM_TRANSPORT_LINEBUF_SIZE];
    int             linebuf_len;
} stratum_transport_esp_ctx_t;

static stratum_transport_esp_ctx_t s_esp_ctx = { .handle = NULL, .tls = false, .linebuf_len = 0 };

// Scan ctx->linebuf[0..linebuf_len) for a newline; on a hit, NUL-terminate
// in place, copy into buf, and shift the remainder (including the consumed
// line + '\n') off the front. Returns true on a complete line.
static bool esp_extract_line(stratum_transport_esp_ctx_t *c, char *buf, size_t buf_cap)
{
    for (int i = 0; i < c->linebuf_len; i++) {
        if (c->linebuf[i] == '\n') {
            size_t copy_len = (size_t)i < buf_cap - 1 ? (size_t)i : buf_cap - 1;
            memcpy(buf, c->linebuf, copy_len);
            buf[copy_len] = '\0';

            int remaining = c->linebuf_len - (i + 1);
            if (remaining > 0) {
                memmove(c->linebuf, c->linebuf + i + 1, (size_t)remaining);
            }
            c->linebuf_len = remaining > 0 ? remaining : 0;
            return true;
        }
    }
    return false;
}

static bool esp_connect(void *ctx, const char *host, uint16_t port)
{
    stratum_transport_esp_ctx_t *c = (stratum_transport_esp_ctx_t *)ctx;

    if (c->handle) {
        bb_tcp_client_destroy(c->handle);
        c->handle = NULL;
    }

    bb_tcp_client_cfg_t cfg = { 0 };
    strncpy(cfg.host, host, sizeof(cfg.host) - 1);
    cfg.port = port;
    cfg.tls = c->tls;

    if (bb_tcp_client_init(&cfg, &c->handle) != BB_OK) {
        return false;
    }
    c->linebuf_len = 0;
    return bb_tcp_client_connect(c->handle) == BB_OK;
}

static stratum_io_result_t esp_read_line(void *ctx, char *buf, size_t buf_cap, uint32_t poll_ms)
{
    stratum_transport_esp_ctx_t *c = (stratum_transport_esp_ctx_t *)ctx;
    if (!c->handle) return STRATUM_IO_ERROR;

    // A line may already be sitting in the accumulator from a previous
    // partial read.
    if (esp_extract_line(c, buf, buf_cap)) {
        return STRATUM_IO_OK;
    }

    bool readable = false;
    if (bb_tcp_client_poll_readable(c->handle, poll_ms, &readable) != BB_OK) {
        return STRATUM_IO_ERROR;
    }
    if (!readable) {
        return STRATUM_IO_TIMEOUT;
    }

    int space = (int)sizeof(c->linebuf) - c->linebuf_len;
    if (space <= 0) {
        // Overflow guard: drop the accumulator rather than growing unbounded.
        c->linebuf_len = 0;
        space = (int)sizeof(c->linebuf);
    }

    size_t n = 0;
    bb_err_t err = bb_tcp_client_read(c->handle, (uint8_t *)(c->linebuf + c->linebuf_len),
                                       (size_t)space, &n);
    if (err == BB_ERR_TIMEOUT) {
        return STRATUM_IO_TIMEOUT;
    }
    if (err != BB_OK) {
        return STRATUM_IO_ERROR;
    }

    c->linebuf_len += (int)n;

    return esp_extract_line(c, buf, buf_cap) ? STRATUM_IO_OK : STRATUM_IO_TIMEOUT;
}

static bool esp_write(void *ctx, const char *msg)
{
    stratum_transport_esp_ctx_t *c = (stratum_transport_esp_ctx_t *)ctx;
    if (!c->handle) return false;
    return bb_tcp_client_write(c->handle, (const uint8_t *)msg, strlen(msg)) == BB_OK;
}

static void esp_close(void *ctx)
{
    stratum_transport_esp_ctx_t *c = (stratum_transport_esp_ctx_t *)ctx;
    if (c->handle) {
        bb_tcp_client_close(c->handle);
    }
}

void stratum_transport_esp_init(stratum_transport_ops_t *ops, bool tls)
{
    s_esp_ctx.tls = tls;
    ops->ctx = &s_esp_ctx;
    ops->connect = esp_connect;
    ops->read_line = esp_read_line;
    ops->write = esp_write;
    ops->close = esp_close;
}

#endif /* ESP_PLATFORM */
