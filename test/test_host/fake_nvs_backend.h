#pragma once

// Shared fake in-memory "nvs" bb_storage backend for host tests exercising
// NVS-backed bb_config field tables (tm_pool_config here) -- the real "nvs"
// bb_storage backend is ESP-IDF-only. Duplicated verbatim from breadboard's
// own test/test_host/fake_nvs_backend.h (bb_settings' host tests) per the
// workspace's "duplicate host tests with TaipanMiner until migration"
// decision -- TM has no live dependency on breadboard's test/ tree, only its
// components/platform sources, so this fake cannot be shared by include path
// and is intentionally re-vendored here instead. Header-only, static-inline:
// each TU that includes this gets its own private copy of the fake's state
// (no cross-TU sharing needed -- every test file registers its own instance
// via fake_nvs_reset()).
//
// Multi-key transactions mirror platform/host/bb_storage_ram/
// bb_storage_ram.c's ram_txn_*: writes stage in txn->_slots (txn-local) and
// land in the fake store only on commit.
//
// Failure injection: fake_nvs_backend_fail_key(key) forces the NEXT get()
// against that key to return BB_ERR_TIMEOUT (one-shot -- cleared after
// firing) -- bb_core.h has no dedicated "storage I/O" error code, so this
// stands in for a generic non-NOT_FOUND backend failure, for exercising
// promote()'s fail-closed backend-error branches.
//
// fake_nvs_backend_fail_set_key(key) is the set()-side counterpart -- same
// one-shot BB_ERR_TIMEOUT injection, for exercising a consumer's
// write-failure branch (e.g. an HTTP handler's 500-on-NV-write-failed path).
//
// fake_nvs_backend_fail_commit() is the txn_commit()-side counterpart --
// same one-shot BB_ERR_TIMEOUT injection, not keyed (a commit spans every
// staged key in the txn, not a single one), for exercising a consumer's
// atomic-commit-failure branch (e.g. B1-807's pending_promote: the ssid/
// pass/try commit itself fails, so nothing -- including the sequential
// provisioned-flag write that follows it -- must run).
//
// fake_nvs_backend_fail_erase_key(key) is the erase()-side counterpart --
// same one-shot BB_ERR_TIMEOUT injection, for exercising a consumer's
// multi-key erase sequence (e.g. tm_pool_stats_reset()'s erase-stat-then-
// erase-score pair) where one key's erase fails after an earlier one
// already succeeded, leaving storage split.

#include "bb_storage.h"

#include <string.h>

// 32 entries -- TM's tm_pool_config exercises all 3 slots x 8 fields (24
// keys) concurrently in its per-idx-isolation test, above breadboard's
// original 8-entry sizing (which only ever needed a handful of bb_settings
// keys live at once).
#define FAKE_NVS_MAX_ENTRIES 32
// 1024 -- bumped from breadboard's original 128 to fit tm_pool_stats's
// "pool%d_score" scoreboard BLOB (TM_POOL_SCOREBOARD_N=10 full share
// records, sizeof(tm_pool_scoreboard_t) ~800 bytes) round-tripping through
// this fake with headroom to spare.
#define FAKE_NVS_MAX_VALUE 1024
#define FAKE_NVS_KEY_MAX   32

typedef struct {
    bool    used;
    char    key[FAKE_NVS_KEY_MAX];
    size_t  len;
    uint8_t value[FAKE_NVS_MAX_VALUE];
} fake_nvs_entry_t;

static fake_nvs_entry_t s_fake_nvs[FAKE_NVS_MAX_ENTRIES];

// One-shot get() failure injection: the next get() against this key returns
// BB_ERR_TIMEOUT instead of touching the store, then clears itself.
static char s_fake_nvs_fail_key[FAKE_NVS_KEY_MAX];

// One-shot set() failure injection -- same one-shot contract as
// s_fake_nvs_fail_key above, applied to set() instead of get(). The count
// lets a caller arm N consecutive failures (fake_nvs_backend_fail_set_key_n)
// instead of just one, e.g. to exercise a retry-then-give-up path where
// every attempt must fail.
static char s_fake_nvs_fail_set_key[FAKE_NVS_KEY_MAX];
static int  s_fake_nvs_fail_set_count;

// One-shot txn_commit() failure injection -- not keyed, see
// fake_nvs_backend_fail_commit() above.
static bool s_fake_nvs_fail_commit;

// One-shot erase() failure injection -- same one-shot contract as
// s_fake_nvs_fail_key, applied to erase() instead of get(). See
// fake_nvs_backend_fail_erase_key() above.
static char s_fake_nvs_fail_erase_key[FAKE_NVS_KEY_MAX];

static inline void fake_nvs_reset(void)
{
    memset(s_fake_nvs, 0, sizeof(s_fake_nvs));
    s_fake_nvs_fail_key[0] = '\0';
    s_fake_nvs_fail_set_key[0] = '\0';
    s_fake_nvs_fail_set_count = 0;
    s_fake_nvs_fail_commit = false;
    s_fake_nvs_fail_erase_key[0] = '\0';
}

// Arms one-shot get() failure injection for the given key. Pass NULL/""
// to disarm without waiting for it to fire.
static inline void fake_nvs_backend_fail_key(const char *key)
{
    if (key == NULL) {
        s_fake_nvs_fail_key[0] = '\0';
        return;
    }
    strncpy(s_fake_nvs_fail_key, key, sizeof(s_fake_nvs_fail_key) - 1);
    s_fake_nvs_fail_key[sizeof(s_fake_nvs_fail_key) - 1] = '\0';
}

// Arms one-shot set() failure injection for the given key. Pass NULL/""
// to disarm without waiting for it to fire.
static inline void fake_nvs_backend_fail_set_key(const char *key)
{
    if (key == NULL) {
        s_fake_nvs_fail_set_key[0] = '\0';
        s_fake_nvs_fail_set_count = 0;
        return;
    }
    strncpy(s_fake_nvs_fail_set_key, key, sizeof(s_fake_nvs_fail_set_key) - 1);
    s_fake_nvs_fail_set_key[sizeof(s_fake_nvs_fail_set_key) - 1] = '\0';
    s_fake_nvs_fail_set_count = 1;
}

// Arms N consecutive set() failures against the given key -- e.g. to
// exercise a retry-then-give-up path where every attempt must fail.
static inline void fake_nvs_backend_fail_set_key_n(const char *key, int times)
{
    if (key == NULL || times <= 0) {
        s_fake_nvs_fail_set_key[0] = '\0';
        s_fake_nvs_fail_set_count = 0;
        return;
    }
    strncpy(s_fake_nvs_fail_set_key, key, sizeof(s_fake_nvs_fail_set_key) - 1);
    s_fake_nvs_fail_set_key[sizeof(s_fake_nvs_fail_set_key) - 1] = '\0';
    s_fake_nvs_fail_set_count = times;
}

// Arms one-shot txn_commit() failure injection -- the NEXT txn_commit()
// call returns BB_ERR_TIMEOUT without landing any of the txn's staged
// slots, then clears itself.
static inline void fake_nvs_backend_fail_commit(void)
{
    s_fake_nvs_fail_commit = true;
}

// Arms one-shot erase() failure injection for the given key. Pass NULL/""
// to disarm without waiting for it to fire.
static inline void fake_nvs_backend_fail_erase_key(const char *key)
{
    if (key == NULL) {
        s_fake_nvs_fail_erase_key[0] = '\0';
        return;
    }
    strncpy(s_fake_nvs_fail_erase_key, key, sizeof(s_fake_nvs_fail_erase_key) - 1);
    s_fake_nvs_fail_erase_key[sizeof(s_fake_nvs_fail_erase_key) - 1] = '\0';
}

static inline fake_nvs_entry_t *fake_nvs_find(const char *key)
{
    if (key == NULL) return NULL;
    for (int i = 0; i < FAKE_NVS_MAX_ENTRIES; i++) {
        if (s_fake_nvs[i].used && strcmp(s_fake_nvs[i].key, key) == 0) {
            return &s_fake_nvs[i];
        }
    }
    return NULL;
}

static inline bb_err_t fake_nvs_get(void *impl, const bb_storage_addr_t *addr, void *buf, size_t cap,
                                     size_t *out_len)
{
    (void)impl;
    if (s_fake_nvs_fail_key[0] != '\0' && addr->key != NULL && strcmp(s_fake_nvs_fail_key, addr->key) == 0) {
        s_fake_nvs_fail_key[0] = '\0';  // one-shot
        return BB_ERR_TIMEOUT;
    }
    fake_nvs_entry_t *e = fake_nvs_find(addr->key);
    if (e == NULL) return BB_ERR_NOT_FOUND;
    *out_len = e->len;
    if (cap > 0) {
        size_t copy_len = e->len < cap ? e->len : cap;
        memcpy(buf, e->value, copy_len);
    }
    return BB_OK;
}

static inline bb_err_t fake_nvs_set(void *impl, const bb_storage_addr_t *addr, const void *buf, size_t len)
{
    (void)impl;
    if (s_fake_nvs_fail_set_key[0] != '\0' && addr->key != NULL &&
        strcmp(s_fake_nvs_fail_set_key, addr->key) == 0) {
        if (--s_fake_nvs_fail_set_count <= 0) {
            s_fake_nvs_fail_set_key[0] = '\0';  // disarm once the count is exhausted
        }
        return BB_ERR_TIMEOUT;
    }
    if (len > FAKE_NVS_MAX_VALUE) return BB_ERR_NO_SPACE;

    fake_nvs_entry_t *e = fake_nvs_find(addr->key);
    if (e == NULL) {
        for (int i = 0; i < FAKE_NVS_MAX_ENTRIES; i++) {
            if (!s_fake_nvs[i].used) { e = &s_fake_nvs[i]; break; }
        }
        if (e == NULL) return BB_ERR_NO_SPACE;
        strncpy(e->key, addr->key, sizeof(e->key) - 1);
        e->key[sizeof(e->key) - 1] = '\0';
        e->used = true;
    }
    if (len > 0) memcpy(e->value, buf, len);
    e->len = len;
    return BB_OK;
}

static inline bb_err_t fake_nvs_erase(void *impl, const bb_storage_addr_t *addr)
{
    (void)impl;
    if (s_fake_nvs_fail_erase_key[0] != '\0' && addr->key != NULL &&
        strcmp(s_fake_nvs_fail_erase_key, addr->key) == 0) {
        s_fake_nvs_fail_erase_key[0] = '\0';  // one-shot
        return BB_ERR_TIMEOUT;
    }
    fake_nvs_entry_t *e = fake_nvs_find(addr->key);
    if (e != NULL) memset(e, 0, sizeof(*e));
    return BB_OK;
}

static inline bool fake_nvs_exists(void *impl, const bb_storage_addr_t *addr)
{
    (void)impl;
    return fake_nvs_find(addr->key) != NULL;
}

static inline bb_err_t fake_nvs_txn_begin(void *impl, bb_storage_txn_t *txn, const char *ns_or_dir)
{
    (void)impl;
    (void)ns_or_dir;
    // txn->_slots is caller-owned backing storage (B1-1235) -- size the
    // clear by txn->_cap, NOT sizeof(txn->_slots) (a pointer now, not an
    // array).
    memset(txn->_slots, 0, txn->_cap * sizeof(txn->_slots[0]));
    txn->_open = 1;
    txn->_err = BB_OK;
    return BB_OK;
}

static inline bb_err_t fake_nvs_txn_set(void *impl, bb_storage_txn_t *txn, const char *key, bb_storage_enc_t enc,
                                         const void *buf, size_t len)
{
    (void)impl;
    (void)enc;
    if (len > FAKE_NVS_MAX_VALUE) return BB_ERR_NO_SPACE;

    for (size_t i = 0; i < txn->_cap; i++) {
        if (txn->_slots[i].used && strcmp(txn->_slots[i].key, key) == 0) {
            if (len > 0) memcpy(txn->_slots[i].value, buf, len);
            txn->_slots[i].len = len;
            txn->_slots[i].enc = enc;
            return BB_OK;
        }
    }
    for (size_t i = 0; i < txn->_cap; i++) {
        if (!txn->_slots[i].used) {
            strncpy(txn->_slots[i].key, key, sizeof(txn->_slots[i].key) - 1);
            txn->_slots[i].key[sizeof(txn->_slots[i].key) - 1] = '\0';
            if (len > 0) memcpy(txn->_slots[i].value, buf, len);
            txn->_slots[i].len = len;
            txn->_slots[i].enc = enc;
            txn->_slots[i].used = true;
            return BB_OK;
        }
    }
    return BB_ERR_NO_SPACE;
}

static inline bb_err_t fake_nvs_txn_commit(void *impl, bb_storage_txn_t *txn)
{
    (void)impl;
    if (s_fake_nvs_fail_commit) {
        s_fake_nvs_fail_commit = false;  // one-shot
        txn->_open = 0;
        return BB_ERR_TIMEOUT;
    }
    if (txn->_err != BB_OK) {
        txn->_open = 0;
        return txn->_err;
    }
    for (size_t i = 0; i < txn->_cap; i++) {
        if (!txn->_slots[i].used) continue;
        bb_err_t err = fake_nvs_set(NULL, &(bb_storage_addr_t){ .key = txn->_slots[i].key },
                                     txn->_slots[i].value, txn->_slots[i].len);
        if (err != BB_OK) {
            txn->_open = 0;
            return err;
        }
    }
    txn->_open = 0;
    return BB_OK;
}

static inline bb_err_t fake_nvs_txn_abort(void *impl, bb_storage_txn_t *txn)
{
    (void)impl;
    txn->_open = 0;
    return BB_OK;
}

static const bb_storage_vtable_t s_fake_nvs_vtable = {
    .get        = fake_nvs_get,
    .set        = fake_nvs_set,
    .erase      = fake_nvs_erase,
    .exists     = fake_nvs_exists,
    .txn_begin  = fake_nvs_txn_begin,
    .txn_set    = fake_nvs_txn_set,
    .txn_commit = fake_nvs_txn_commit,
    .txn_abort  = fake_nvs_txn_abort,
};
