#pragma once

// Shared fake stratum_transport_ops_t double for host tests -- NOT
// bb_tcp_client. See stratum_transport.h's header comment for why the FSM
// is behind a small ops vtable rather than calling bb_tcp_client directly.
//
// `static` (internal linkage): every .c file that includes this header gets
// its OWN private copy of s_ft and every function below -- test isolation
// across test files is preserved exactly as it was when each file carried
// its own copy of this fixture, but the IMPLEMENTATION TEXT now lives in
// exactly one place (TA-571 firmware review finding #6: this was previously
// duplicated verbatim between test_stratum_fsm.c and
// test_stratum_pool_client.c).
#include "stratum_transport.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    bool        connect_result;
    int         connect_calls;
    char        writes[8][300];
    int         write_count;
    const char *pending_lines[8];
    int         pending_count;
    int         pending_idx;
    bool        io_error;
    bool        closed;
    int         close_calls;
} fake_transport_t;

static fake_transport_t s_ft;

static bool fake_connect(void *ctx, const char *host, uint16_t port)
{
    (void)ctx; (void)host; (void)port;
    s_ft.connect_calls++;
    s_ft.closed = false;
    return s_ft.connect_result;
}

static stratum_io_result_t fake_read_line(void *ctx, char *buf, size_t cap, uint32_t poll_ms)
{
    (void)ctx; (void)poll_ms;
    if (s_ft.io_error) return STRATUM_IO_ERROR;
    if (s_ft.pending_idx < s_ft.pending_count) {
        strncpy(buf, s_ft.pending_lines[s_ft.pending_idx++], cap - 1);
        buf[cap - 1] = '\0';
        return STRATUM_IO_OK;
    }
    return STRATUM_IO_TIMEOUT;
}

static bool fake_write(void *ctx, const char *msg)
{
    (void)ctx;
    if (s_ft.write_count < 8) {
        strncpy(s_ft.writes[s_ft.write_count], msg, 299);
        s_ft.writes[s_ft.write_count][299] = '\0';
    }
    s_ft.write_count++;
    return true;
}

static void fake_close(void *ctx)
{
    (void)ctx;
    s_ft.closed = true;
    s_ft.close_calls++;
}

// Zero the fixture and set the default (successful) connect outcome. Call
// from setUp()/reset_fakes() before every test.
static void fake_transport_reset(void)
{
    memset(&s_ft, 0, sizeof(s_ft));
    s_ft.connect_result = true;
}

// Bind *ops to the fake functions above.
static void fake_transport_bind(stratum_transport_ops_t *ops)
{
    ops->ctx = NULL;
    ops->connect = fake_connect;
    ops->read_line = fake_read_line;
    ops->write = fake_write;
    ops->close = fake_close;
}
