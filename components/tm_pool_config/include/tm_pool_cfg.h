#pragma once

// tm_pool_config -- 3-slot pool configuration (host/port/wallet/worker/pass/
// SV1-vs-SV2 protocol selection), persisted via bb_config field tables over
// the "nvs" bb_storage backend, namespace "tm_pool". Fresh schema (NOT
// byte-compat with v1's components/config "pool_host"/"pool_host_2" 2-slot
// NVS layout, origin/main:components/config/src/config.c) -- this component
// matches v1's FUNCTIONAL behavior only (is_configured predicate,
// sequential-write early-return contract), not its on-flash key names.
//
// No platform types leak here: bb_err_t (bb_core) is the only breadboard
// type this header depends on. bb_config/bb_storage stay internal to
// src/tm_pool_config.c.

#include "bb_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TM_POOL_MAX          3
#define TM_POOL_HOST_MAX     96
#define TM_POOL_WALLET_MAX   96
#define TM_POOL_WORKER_MAX   40
#define TM_POOL_PASS_MAX     64
#define TM_POOL_SV2_KEY_MAX  64

typedef enum {
    TM_POOL_PROTO_STRATUM_V1 = 0,
    TM_POOL_PROTO_STRATUM_V2 = 1,
} tm_pool_proto_t;

typedef struct {
    char            host[TM_POOL_HOST_MAX];
    uint16_t        port;
    char            wallet[TM_POOL_WALLET_MAX];
    char            worker[TM_POOL_WORKER_MAX];
    char            pass[TM_POOL_PASS_MAX];
    bool            extranonce_subscribe;
    tm_pool_proto_t protocol;
    char            sv2_authority_pubkey[TM_POOL_SV2_KEY_MAX];
    bool            sv2_authority_pubkey_set;
} tm_pool_cfg_t;

// Composition-only: the per-slot bb_config_field_t tables are static const
// data resolved against bb_storage on every accessor call (bb_config's own
// no-registry design) -- there is nothing to register here. This function
// exists so a composer has a single, uniform init-tier call site alongside
// every other tm_/bb_ component, and so a later PR that adds real
// preparation work (e.g. schema validation) has a seam to land in without an
// API break. Always returns BB_OK today.
bb_err_t tm_pool_config_init(void);

// Load slot `idx` from NVS into *out. Unset scalar/string fields with a
// declared default (pass -> "x", protocol -> TM_POOL_PROTO_STRATUM_V1)
// resolve to that default; unset fields with NO declared default (host,
// port, wallet, worker, sv2_authority_pubkey) resolve to a zeroed/empty
// value -- this function never itself distinguishes "unset" from "unstored
// default" the way a raw bb_config accessor would; use
// tm_pool_config_is_configured() for the "does this slot have a real pool"
// question. sv2_authority_pubkey_set reports true iff a NON-EMPTY value is
// currently stored (same non-empty-value convention as
// tm_pool_config_is_configured/v1's config_pool_configured) -- an explicitly
// stored empty string round-trips as sv2_authority_pubkey_set=false.
// Returns:
//   BB_OK                 loaded (*out fully populated, see defaulting above)
//   BB_ERR_INVALID_ARG    out is NULL, or idx >= TM_POOL_MAX
//   (other)               a genuine backend I/O error propagated from the
//                         underlying bb_config/bb_storage read
bb_err_t tm_pool_config_get(uint8_t idx, tm_pool_cfg_t *out);

// Persist *cfg into slot `idx`. Writes each field sequentially, in
// declaration order (host, port, wallet, worker, pass, extranonce_subscribe,
// protocol, sv2_authority_pubkey), returning on the FIRST field write that
// fails -- mirrors v1's config_set_pools early-return contract exactly; this
// is NOT an atomic multi-field commit (a failure partway through can leave
// the slot with a mix of old and new field values -- same accepted risk v1
// carried; PINNED by test_tm_pool_config_set_partial_write_on_mid_sequence_
// fault_is_v1_parity in test/test_host/test_tm_pool_config.c). The
// sv2_authority_pubkey field is written when cfg->sv2_authority_pubkey_set
// is true AND cfg->sv2_authority_pubkey is non-empty, else erased (idempotent
// -- erasing an absent key is BB_OK) -- an empty string with
// sv2_authority_pubkey_set=true is normalized to the erase path too, so the
// stored state always matches what tm_pool_config_get() will report back
// (set=true with an empty value can never round-trip, per its
// non-empty-value contract; see that function's doc comment).
// Returns:
//   BB_OK                 every field persisted
//   BB_ERR_INVALID_ARG    cfg is NULL, or idx >= TM_POOL_MAX
//   (other)               the first field write/erase's own error
bb_err_t tm_pool_config_set(uint8_t idx, const tm_pool_cfg_t *cfg);

// True iff slot `idx` has a real pool configured: host, port, wallet, and
// worker are ALL non-empty/non-zero (same predicate v1's
// config_pool_configured used, applied to this component's own 3-slot
// schema). False for an out-of-range idx or a genuine backend read error
// (fail-closed -- a storage fault is never reported as "configured").
bool tm_pool_config_is_configured(uint8_t idx);

#ifdef __cplusplus
}
#endif
