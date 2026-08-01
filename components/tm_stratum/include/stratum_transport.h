#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Thin ops-vtable over bb_tcp_client (the mandated DUMB transport -- connect/
// read/write/close only; reconnect, backoff, and keepalive live in the FSM,
// never here). The production backend (stratum_transport_esp.c, compiled
// only under ESP_PLATFORM) wraps bb_tcp_client_* directly one-to-one.
//
// Why a vtable instead of calling bb_tcp_client_* straight from
// stratum_fsm.c: bb_tcp_client's own host backend pulls in its PRIV_REQUIRES
// chain (bb_nv, bb_transport_health, freertos-shaped locking) purely for
// config-load/health-report bookkeeping that stratum's FSM tests have no
// need to exercise. The vtable keeps stratum_fsm.c host-testable with a
// small in-test fake (see test_stratum_fsm.c) while the ESP_PLATFORM
// backend is a straight pass-through to the real bb_tcp_client -- the FSM's
// production code path genuinely runs over bb_tcp_client, satisfying the
// "transport is bb_tcp_client, full stop" requirement.

typedef enum {
    STRATUM_IO_OK = 0,
    STRATUM_IO_TIMEOUT,   // no data / would-block; not a failure
    STRATUM_IO_ERROR,     // hard transport error
} stratum_io_result_t;

typedef struct {
    void *ctx;

    // Blocking, bounded by the backend's own connect timeout.
    bool (*connect)(void *ctx, const char *host, uint16_t port);

    // Attempt to read one newline-terminated line into buf (capacity
    // buf_cap, NUL-terminated on success). Non-blocking-friendly: polls for
    // at most poll_ms and returns STRATUM_IO_TIMEOUT if nothing arrived.
    stratum_io_result_t (*read_line)(void *ctx, char *buf, size_t buf_cap, uint32_t poll_ms);

    // Write a NUL-terminated message. Returns false on a hard transport
    // error.
    bool (*write)(void *ctx, const char *msg);

    // Idempotent; safe to call when already disconnected.
    void (*close)(void *ctx);
} stratum_transport_ops_t;

#ifdef ESP_PLATFORM
// Production ops backed by the real bb_tcp_client. `host`/`port` are
// supplied per-connect via ops->connect(); tls toggles TLS. Loads pool
// host/port/tls from the "tm_stratum" NVS namespace (see bb_tcp_client_init's
// NULL-cfg path) -- the operator provisions those keys directly.
void stratum_transport_esp_init(stratum_transport_ops_t *ops, bool tls);
#endif
