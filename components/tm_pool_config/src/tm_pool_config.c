// tm_pool_config -- see include/tm_pool_cfg.h for the public contract.
//
// Field tables target the "nvs" bb_storage backend, namespace "tm_pool"
// (TM_POOL_NVS_NS below). Keys are "pool%d_<field>" for idx in
// [0, TM_POOL_MAX) -- every key stays under the real NVS 15-char (+NUL)
// key-name limit (NVS_KEY_NAME_MAX_SIZE=16; see bb_storage.h's
// BB_STORAGE_TXN_KEY_MAX_BYTES comment for where that ceiling is documented)
// with margin to spare (longest key here is "pool0_wallet" at 12 chars).
//
// Per-slot field tables are macro-generated (TM_POOL_DEFINE_SLOT(n)) to
// avoid hand-duplicating 8 field descriptors x 3 slots -- the consolidation
// rule (2nd hand-rolled instance triggers extraction) applies to a would-be
// 3rd copy-pasted slot just as much as to a shared helper. Sequential
// (non-transactional) writes in tm_pool_config_set() mirror v1
// components/config's config_set_pools early-return contract exactly
// (origin/main:components/config/src/config.c) -- a write that fails
// partway through can leave a slot with a mix of old and new field values,
// same accepted risk v1 carried; see test_tm_pool_config_set_partial_write_
// on_mid_sequence_fault_is_v1_parity (test/test_host/test_tm_pool_config.c)
// for the pinned behavior. bb_config_staged's atomic txn path is
// deliberately NOT used here because its slot value capacity
// (BB_STORAGE_TXN_VALUE_MAX_BYTES, default 64) is smaller than this
// component's own host/wallet fields (96 bytes) -- a staged commit would
// spuriously BB_ERR_NO_SPACE on a legitimately long host/wallet value.

#include "tm_pool_cfg.h"

#include "bb_config.h"

#include <string.h>

#define TM_POOL_NVS_NS "tm_pool"

typedef struct {
    const bb_config_field_t *host;
    const bb_config_field_t *port;
    const bb_config_field_t *wallet;
    const bb_config_field_t *worker;
    const bb_config_field_t *pass;
    const bb_config_field_t *xnsub;
    const bb_config_field_t *proto;
    const bb_config_field_t *sv2key;
} tm_pool_field_set_t;

// Generates the 8 static const bb_config_field_t descriptors for slot `n`
// (n is a literal digit token: 0, 1, 2 -- TM_POOL_MAX). Adjacent
// string-literal concatenation ("pool" #n "_host") builds each NVS key at
// compile time; #n stringizes the literal digit passed at each invocation
// site below.
#define TM_POOL_DEFINE_SLOT(n) \
    static const bb_config_field_t s_pool##n##_host = { \
        .id = "pool" #n ".host", .type = BB_CONFIG_STR, \
        .addr = { .backend = "nvs", .ns_or_dir = TM_POOL_NVS_NS, .key = "pool" #n "_host" }, \
        .max_len = TM_POOL_HOST_MAX, \
    }; \
    static const bb_config_field_t s_pool##n##_port = { \
        .id = "pool" #n ".port", .type = BB_CONFIG_U16, \
        .addr = { .backend = "nvs", .ns_or_dir = TM_POOL_NVS_NS, .key = "pool" #n "_port" }, \
    }; \
    static const bb_config_field_t s_pool##n##_wallet = { \
        .id = "pool" #n ".wallet", .type = BB_CONFIG_STR, \
        .addr = { .backend = "nvs", .ns_or_dir = TM_POOL_NVS_NS, .key = "pool" #n "_wallet" }, \
        .max_len = TM_POOL_WALLET_MAX, \
    }; \
    static const bb_config_field_t s_pool##n##_worker = { \
        .id = "pool" #n ".worker", .type = BB_CONFIG_STR, \
        .addr = { .backend = "nvs", .ns_or_dir = TM_POOL_NVS_NS, .key = "pool" #n "_worker" }, \
        .max_len = TM_POOL_WORKER_MAX, \
    }; \
    static const bb_config_field_t s_pool##n##_pass = { \
        .id = "pool" #n ".pass", .type = BB_CONFIG_STR, \
        .addr = { .backend = "nvs", .ns_or_dir = TM_POOL_NVS_NS, .key = "pool" #n "_pass" }, \
        .max_len = TM_POOL_PASS_MAX, .has_default = true, .def = { .str = "x" }, \
    }; \
    static const bb_config_field_t s_pool##n##_xnsub = { \
        .id = "pool" #n ".xnsub", .type = BB_CONFIG_BOOL, \
        .addr = { .backend = "nvs", .ns_or_dir = TM_POOL_NVS_NS, .key = "pool" #n "_xnsub" }, \
        .has_default = true, .def = { .b = false }, \
    }; \
    static const bb_config_field_t s_pool##n##_proto = { \
        .id = "pool" #n ".proto", .type = BB_CONFIG_U8, \
        .addr = { .backend = "nvs", .ns_or_dir = TM_POOL_NVS_NS, .key = "pool" #n "_proto" }, \
        .has_default = true, .def = { .u8 = (uint8_t)TM_POOL_PROTO_STRATUM_V1 }, \
    }; \
    static const bb_config_field_t s_pool##n##_sv2key = { \
        .id = "pool" #n ".sv2key", .type = BB_CONFIG_STR, \
        .addr = { .backend = "nvs", .ns_or_dir = TM_POOL_NVS_NS, .key = "pool" #n "_sv2key" }, \
        .max_len = TM_POOL_SV2_KEY_MAX, \
    }

TM_POOL_DEFINE_SLOT(0);
TM_POOL_DEFINE_SLOT(1);
TM_POOL_DEFINE_SLOT(2);

// TM_POOL_MAX-sized -- extending TM_POOL_MAX requires a new
// TM_POOL_DEFINE_SLOT(n) invocation above plus a new row here (both are
// compile-time, so a mismatch is a build error, not a silent runtime gap).
static const tm_pool_field_set_t s_pool_fields[TM_POOL_MAX] = {
    { &s_pool0_host, &s_pool0_port, &s_pool0_wallet, &s_pool0_worker,
      &s_pool0_pass, &s_pool0_xnsub, &s_pool0_proto, &s_pool0_sv2key },
    { &s_pool1_host, &s_pool1_port, &s_pool1_wallet, &s_pool1_worker,
      &s_pool1_pass, &s_pool1_xnsub, &s_pool1_proto, &s_pool1_sv2key },
    { &s_pool2_host, &s_pool2_port, &s_pool2_wallet, &s_pool2_worker,
      &s_pool2_pass, &s_pool2_xnsub, &s_pool2_proto, &s_pool2_sv2key },
};

bb_err_t tm_pool_config_init(void)
{
    // Nothing to register -- see the header's doc comment.
    return BB_OK;
}

bb_err_t tm_pool_config_get(uint8_t idx, tm_pool_cfg_t *out)
{
    if (out == NULL || idx >= TM_POOL_MAX) {
        return BB_ERR_INVALID_ARG;
    }
    const tm_pool_field_set_t *f = &s_pool_fields[idx];
    memset(out, 0, sizeof(*out));

    bb_err_t err;
    size_t   len = 0;

    // host/wallet/worker: no declared default -- NOT_FOUND resolves to the
    // already-zeroed buffer (empty string), same as an unset value always
    // meant here; any OTHER error is a genuine backend fault, propagated.
    err = bb_config_get_str(f->host, out->host, sizeof(out->host), &len);
    if (err != BB_OK && err != BB_ERR_NOT_FOUND) {
        return err;
    }

    uint16_t port = 0;
    err = bb_config_get_u16(f->port, &port);
    if (err != BB_OK && err != BB_ERR_NOT_FOUND) {
        return err;
    }
    out->port = (err == BB_OK) ? port : 0;

    err = bb_config_get_str(f->wallet, out->wallet, sizeof(out->wallet), &len);
    if (err != BB_OK && err != BB_ERR_NOT_FOUND) {
        return err;
    }

    err = bb_config_get_str(f->worker, out->worker, sizeof(out->worker), &len);
    if (err != BB_OK && err != BB_ERR_NOT_FOUND) {
        return err;
    }

    // pass/xnsub/proto all carry a declared default -- bb_config already
    // resolves NOT_FOUND to BB_OK+default, so any non-OK here is a real
    // backend fault.
    err = bb_config_get_str(f->pass, out->pass, sizeof(out->pass), &len);
    if (err != BB_OK) {
        return err;
    }

    bool xnsub = false;
    err = bb_config_get_bool(f->xnsub, &xnsub);
    if (err != BB_OK) {
        return err;
    }
    out->extranonce_subscribe = xnsub;

    uint8_t proto = (uint8_t)TM_POOL_PROTO_STRATUM_V1;
    err = bb_config_get_u8(f->proto, &proto);
    if (err != BB_OK) {
        return err;
    }
    out->protocol = (tm_pool_proto_t)proto;

    // sv2_authority_pubkey: no declared default -- NOT_FOUND means "unset",
    // resolved to the zeroed buffer + sv2_authority_pubkey_set=false. A
    // stored-but-empty value also reports set=false (non-empty-value
    // convention, see the header's doc comment).
    len = 0;
    err = bb_config_get_str(f->sv2key, out->sv2_authority_pubkey, sizeof(out->sv2_authority_pubkey), &len);
    if (err != BB_OK && err != BB_ERR_NOT_FOUND) {
        return err;
    }
    out->sv2_authority_pubkey_set = (err == BB_OK && len > 0);

    return BB_OK;
}

bb_err_t tm_pool_config_set(uint8_t idx, const tm_pool_cfg_t *cfg)
{
    if (cfg == NULL || idx >= TM_POOL_MAX) {
        return BB_ERR_INVALID_ARG;
    }
    const tm_pool_field_set_t *f = &s_pool_fields[idx];
    bb_err_t err;

    err = bb_config_set_str(f->host, cfg->host);
    if (err != BB_OK) {
        return err;
    }
    err = bb_config_set_u16(f->port, cfg->port);
    if (err != BB_OK) {
        return err;
    }
    err = bb_config_set_str(f->wallet, cfg->wallet);
    if (err != BB_OK) {
        return err;
    }
    err = bb_config_set_str(f->worker, cfg->worker);
    if (err != BB_OK) {
        return err;
    }
    err = bb_config_set_str(f->pass, cfg->pass);
    if (err != BB_OK) {
        return err;
    }
    err = bb_config_set_bool(f->xnsub, cfg->extranonce_subscribe);
    if (err != BB_OK) {
        return err;
    }
    err = bb_config_set_u8(f->proto, (uint8_t)cfg->protocol);
    if (err != BB_OK) {
        return err;
    }

    // A "set" empty string normalizes to the SAME erase path as
    // sv2_authority_pubkey_set=false: tm_pool_config_get() reports
    // sv2_authority_pubkey_set=true only for a NON-EMPTY stored value (see
    // the header's doc comment), so writing an empty string under set=true
    // would otherwise round-trip back as set=false -- a caller's explicit
    // "set" flag silently dropped. Normalizing here keeps set()/get()
    // symmetric for every input, not just the non-empty case.
    if (cfg->sv2_authority_pubkey_set && cfg->sv2_authority_pubkey[0] != '\0') {
        err = bb_config_set_str(f->sv2key, cfg->sv2_authority_pubkey);
    } else {
        // Idempotent -- erasing an absent key is BB_OK. Keeps get()'s
        // NOT_FOUND-means-unset contract accurate for a caller that clears
        // the SV2 key.
        err = bb_config_erase(f->sv2key);
    }
    if (err != BB_OK) {
        return err;
    }

    return BB_OK;
}

bool tm_pool_config_is_configured(uint8_t idx)
{
    if (idx >= TM_POOL_MAX) {
        return false;
    }
    tm_pool_cfg_t cfg;
    if (tm_pool_config_get(idx, &cfg) != BB_OK) {
        return false;
    }
    return cfg.host[0] != '\0' && cfg.port != 0 && cfg.wallet[0] != '\0' && cfg.worker[0] != '\0';
}
