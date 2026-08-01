#include "unity.h"
#include "tm_pool_cfg.h"
#include "bb_storage.h"
#include "fake_nvs_backend.h"

#include <string.h>

// tm_pool_config's field tables target backend="nvs" (the real ESP-IDF
// backend) -- host tests register the shared fake in-memory "nvs" vtable
// (see fake_nvs_backend.h) under that same name, exercising the SAME
// production field-table/addr code path the device build uses, with only
// the backend's storage swapped for a host-safe stand-in.

static void reset_all(void)
{
    bb_storage_test_reset();
    fake_nvs_reset();
    bb_storage_register_backend("nvs", &s_fake_nvs_vtable, NULL);
}

static tm_pool_cfg_t make_cfg(const char *host, uint16_t port, const char *wallet,
                               const char *worker, const char *pass, bool xnsub,
                               tm_pool_proto_t proto)
{
    tm_pool_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.host, host, sizeof(cfg.host) - 1);
    cfg.port = port;
    strncpy(cfg.wallet, wallet, sizeof(cfg.wallet) - 1);
    strncpy(cfg.worker, worker, sizeof(cfg.worker) - 1);
    strncpy(cfg.pass, pass, sizeof(cfg.pass) - 1);
    cfg.extranonce_subscribe = xnsub;
    cfg.protocol = proto;
    return cfg;
}

/* ---------------------------------------------------------------------------
 * init
 * ---------------------------------------------------------------------------*/
void test_tm_pool_config_init_returns_ok(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_init());
}

/* ---------------------------------------------------------------------------
 * round-trip set -> get, per slot
 * ---------------------------------------------------------------------------*/
void test_tm_pool_config_set_then_get_round_trip_slot0(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("pool.example.com", 3333, "bc1qexamplewallet",
                                  "rig01", "x", true, TM_POOL_PROTO_STRATUM_V1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));

    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(0, &out));
    TEST_ASSERT_EQUAL_STRING("pool.example.com", out.host);
    TEST_ASSERT_EQUAL_UINT16(3333, out.port);
    TEST_ASSERT_EQUAL_STRING("bc1qexamplewallet", out.wallet);
    TEST_ASSERT_EQUAL_STRING("rig01", out.worker);
    TEST_ASSERT_EQUAL_STRING("x", out.pass);
    TEST_ASSERT_TRUE(out.extranonce_subscribe);
    TEST_ASSERT_EQUAL(TM_POOL_PROTO_STRATUM_V1, out.protocol);
}

void test_tm_pool_config_set_then_get_round_trip_slot2(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("backup.pool.example.com", 4444, "bc1qbackupwallet",
                                  "rig02", "hunter2", false, TM_POOL_PROTO_STRATUM_V2);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(2, &cfg));

    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(2, &out));
    TEST_ASSERT_EQUAL_STRING("backup.pool.example.com", out.host);
    TEST_ASSERT_EQUAL_UINT16(4444, out.port);
    TEST_ASSERT_EQUAL_STRING("bc1qbackupwallet", out.wallet);
    TEST_ASSERT_EQUAL_STRING("rig02", out.worker);
    TEST_ASSERT_EQUAL_STRING("hunter2", out.pass);
    TEST_ASSERT_FALSE(out.extranonce_subscribe);
    TEST_ASSERT_EQUAL(TM_POOL_PROTO_STRATUM_V2, out.protocol);
}

/* ---------------------------------------------------------------------------
 * defaults on unset slot
 * ---------------------------------------------------------------------------*/
void test_tm_pool_config_get_unset_slot_returns_ok_with_defaults(void)
{
    reset_all();
    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(1, &out));
    TEST_ASSERT_EQUAL_STRING("", out.host);
    TEST_ASSERT_EQUAL_UINT16(0, out.port);
    TEST_ASSERT_EQUAL_STRING("", out.wallet);
    TEST_ASSERT_EQUAL_STRING("", out.worker);
    TEST_ASSERT_EQUAL_STRING("x", out.pass);
    TEST_ASSERT_FALSE(out.extranonce_subscribe);
    TEST_ASSERT_EQUAL(TM_POOL_PROTO_STRATUM_V1, out.protocol);
    TEST_ASSERT_FALSE(out.sv2_authority_pubkey_set);
    TEST_ASSERT_EQUAL_STRING("", out.sv2_authority_pubkey);
}

/* ---------------------------------------------------------------------------
 * is_configured
 * ---------------------------------------------------------------------------*/
void test_tm_pool_config_is_configured_false_on_unset_slot(void)
{
    reset_all();
    TEST_ASSERT_FALSE(tm_pool_config_is_configured(0));
}

void test_tm_pool_config_is_configured_true_after_full_set(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("pool.example.com", 3333, "bc1qexamplewallet",
                                  "rig01", "x", false, TM_POOL_PROTO_STRATUM_V1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));
    TEST_ASSERT_TRUE(tm_pool_config_is_configured(0));
}

void test_tm_pool_config_is_configured_false_when_port_zero(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("pool.example.com", 0, "bc1qexamplewallet",
                                  "rig01", "x", false, TM_POOL_PROTO_STRATUM_V1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));
    TEST_ASSERT_FALSE(tm_pool_config_is_configured(0));
}

void test_tm_pool_config_is_configured_false_when_worker_empty(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("pool.example.com", 3333, "bc1qexamplewallet",
                                  "", "x", false, TM_POOL_PROTO_STRATUM_V1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));
    TEST_ASSERT_FALSE(tm_pool_config_is_configured(0));
}

/* ---------------------------------------------------------------------------
 * per-idx isolation
 * ---------------------------------------------------------------------------*/
void test_tm_pool_config_set_slot1_does_not_touch_slot0_or_slot2(void)
{
    reset_all();
    tm_pool_cfg_t seed0 = make_cfg("primary.example.com", 3333, "bc1qprimary",
                                    "rig-primary", "x", false, TM_POOL_PROTO_STRATUM_V1);
    tm_pool_cfg_t seed2 = make_cfg("backup.example.com", 4444, "bc1qbackup",
                                    "rig-backup", "y", true, TM_POOL_PROTO_STRATUM_V2);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &seed0));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(2, &seed2));

    tm_pool_cfg_t mid = make_cfg("fallback.example.com", 5555, "bc1qfallback",
                                  "rig-fallback", "z", false, TM_POOL_PROTO_STRATUM_V1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(1, &mid));

    tm_pool_cfg_t out0, out2;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(0, &out0));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(2, &out2));
    TEST_ASSERT_EQUAL_STRING("primary.example.com", out0.host);
    TEST_ASSERT_EQUAL_UINT16(3333, out0.port);
    TEST_ASSERT_EQUAL_STRING("backup.example.com", out2.host);
    TEST_ASSERT_EQUAL_UINT16(4444, out2.port);
}

/* ---------------------------------------------------------------------------
 * out-of-range idx
 * ---------------------------------------------------------------------------*/
void test_tm_pool_config_get_rejects_out_of_range_idx(void)
{
    reset_all();
    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_config_get(TM_POOL_MAX, &out));
}

void test_tm_pool_config_set_rejects_out_of_range_idx(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("pool.example.com", 3333, "bc1qexamplewallet",
                                  "rig01", "x", false, TM_POOL_PROTO_STRATUM_V1);
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_config_set(TM_POOL_MAX, &cfg));
}

void test_tm_pool_config_is_configured_false_for_out_of_range_idx(void)
{
    reset_all();
    TEST_ASSERT_FALSE(tm_pool_config_is_configured(TM_POOL_MAX));
}

void test_tm_pool_config_get_rejects_null_out(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_config_get(0, NULL));
}

void test_tm_pool_config_set_rejects_null_cfg(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_config_set(0, NULL));
}

/* ---------------------------------------------------------------------------
 * SV2 authority pubkey round-trip
 * ---------------------------------------------------------------------------*/
void test_tm_pool_config_sv2_key_round_trips_when_set(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("sv2.example.com", 3336, "bc1qexamplewallet",
                                  "rig01", "x", false, TM_POOL_PROTO_STRATUM_V2);
    strncpy(cfg.sv2_authority_pubkey, "0123456789abcdef0123456789abcdef",
            sizeof(cfg.sv2_authority_pubkey) - 1);
    cfg.sv2_authority_pubkey_set = true;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));

    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(0, &out));
    TEST_ASSERT_TRUE(out.sv2_authority_pubkey_set);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", out.sv2_authority_pubkey);
    TEST_ASSERT_EQUAL(TM_POOL_PROTO_STRATUM_V2, out.protocol);
}

void test_tm_pool_config_sv2_key_absent_when_unset(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("sv1.example.com", 3333, "bc1qexamplewallet",
                                  "rig01", "x", false, TM_POOL_PROTO_STRATUM_V1);
    /* cfg.sv2_authority_pubkey_set defaults to false via make_cfg's memset. */
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));

    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(0, &out));
    TEST_ASSERT_FALSE(out.sv2_authority_pubkey_set);
    TEST_ASSERT_EQUAL_STRING("", out.sv2_authority_pubkey);
}

void test_tm_pool_config_sv2_key_cleared_by_reset_to_unset(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("sv2.example.com", 3336, "bc1qexamplewallet",
                                  "rig01", "x", false, TM_POOL_PROTO_STRATUM_V2);
    strncpy(cfg.sv2_authority_pubkey, "deadbeef", sizeof(cfg.sv2_authority_pubkey) - 1);
    cfg.sv2_authority_pubkey_set = true;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));

    /* Re-set with sv2_authority_pubkey_set=false -- must erase, not carry
     * forward the previously-stored key. */
    cfg.sv2_authority_pubkey_set = false;
    memset(cfg.sv2_authority_pubkey, 0, sizeof(cfg.sv2_authority_pubkey));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));

    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(0, &out));
    TEST_ASSERT_FALSE(out.sv2_authority_pubkey_set);
    TEST_ASSERT_EQUAL_STRING("", out.sv2_authority_pubkey);
}

// sv2_authority_pubkey_set=true with an EMPTY string must normalize to the
// same erase path as sv2_authority_pubkey_set=false -- get()'s non-empty-
// value contract means a stored empty string could never round-trip as
// set=true anyway, so set() must not silently accept an inconsistent input.
void test_tm_pool_config_sv2_key_set_true_with_empty_string_normalizes_to_erased(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("sv2.example.com", 3336, "bc1qexamplewallet",
                                  "rig01", "x", false, TM_POOL_PROTO_STRATUM_V2);
    /* cfg.sv2_authority_pubkey is already "" via make_cfg's memset. */
    cfg.sv2_authority_pubkey_set = true;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));

    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(0, &out));
    TEST_ASSERT_FALSE(out.sv2_authority_pubkey_set);
    TEST_ASSERT_EQUAL_STRING("", out.sv2_authority_pubkey);
    TEST_ASSERT_FALSE(bb_storage_exists(&(bb_storage_addr_t){
        .backend = "nvs", .ns_or_dir = NULL, .key = "pool0_sv2key" }));
}

/* ---------------------------------------------------------------------------
 * Fail-injection: get()-path, set()-path early-return, is_configured()
 * fail-closed, and the pinned partial-write accepted-risk contract.
 * ---------------------------------------------------------------------------*/

// A genuine backend read fault on the FIRST field get() touches must
// propagate as-is -- never silently absorbed the way a NOT_FOUND is.
void test_tm_pool_config_get_propagates_genuine_backend_fault(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("pool.example.com", 3333, "bc1qexamplewallet",
                                  "rig01", "x", false, TM_POOL_PROTO_STRATUM_V1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));

    fake_nvs_backend_fail_key("pool0_host");
    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_config_get(0, &out));
}

// set()'s documented early-return contract: a fault on the 3rd field
// (wallet) must stop the sequence there -- host/port (staged before it)
// land, worker/pass/xnsub/proto/sv2key (staged after it) are never
// attempted, so the slot keeps whatever it held for those before this call.
void test_tm_pool_config_set_stops_at_first_failing_field(void)
{
    reset_all();
    tm_pool_cfg_t seed = make_cfg("original.example.com", 1111, "bc1qoriginalwallet",
                                   "original-rig", "origpass", false, TM_POOL_PROTO_STRATUM_V1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &seed));

    tm_pool_cfg_t attempt = make_cfg("updated.example.com", 2222, "bc1qupdatedwallet",
                                      "updated-rig", "newpass", true, TM_POOL_PROTO_STRATUM_V2);
    fake_nvs_backend_fail_set_key("pool0_wallet");
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_config_set(0, &attempt));

    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(0, &out));
    /* host/port: staged BEFORE the failing field -- new values landed. */
    TEST_ASSERT_EQUAL_STRING("updated.example.com", out.host);
    TEST_ASSERT_EQUAL_UINT16(2222, out.port);
    /* wallet: the failing field itself -- unchanged (old value). */
    TEST_ASSERT_EQUAL_STRING("bc1qoriginalwallet", out.wallet);
    /* worker/pass/xnsub/proto: staged AFTER the failing field -- never
     * attempted, so they still hold the seeded values, not the attempt's. */
    TEST_ASSERT_EQUAL_STRING("original-rig", out.worker);
    TEST_ASSERT_EQUAL_STRING("origpass", out.pass);
    TEST_ASSERT_FALSE(out.extranonce_subscribe);
    TEST_ASSERT_EQUAL(TM_POOL_PROTO_STRATUM_V1, out.protocol);
}

// is_configured() must fail-closed on a genuine backend read error, not just
// an out-of-range idx -- distinct from
// test_tm_pool_config_is_configured_false_for_out_of_range_idx.
void test_tm_pool_config_is_configured_false_on_genuine_backend_fault(void)
{
    reset_all();
    tm_pool_cfg_t cfg = make_cfg("pool.example.com", 3333, "bc1qexamplewallet",
                                  "rig01", "x", false, TM_POOL_PROTO_STRATUM_V1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &cfg));
    TEST_ASSERT_TRUE(tm_pool_config_is_configured(0));

    fake_nvs_backend_fail_key("pool0_host");
    TEST_ASSERT_FALSE(tm_pool_config_is_configured(0));
}

// PINS the accepted-risk documented on tm_pool_config_set()'s early-return
// contract (see include/tm_pool_cfg.h and src/tm_pool_config.c's file
// header): a mid-sequence fault can leave a slot with a mix of old and new
// field values (here: new host, OLD wallet) that is STILL a fully-configured
// slot per tm_pool_config_is_configured()'s predicate (host/port/wallet/
// worker all non-empty/non-zero) -- a silent cross-wired identity, same
// v1-parity risk v1's config_set_pools always carried. A future PR that
// wants to close this gap (e.g. by moving to bb_config_staged with a wider
// slot-value cap) must update this test, not silently break it.
void test_tm_pool_config_set_partial_write_on_mid_sequence_fault_is_v1_parity(void)
{
    reset_all();
    tm_pool_cfg_t seed = make_cfg("original.example.com", 3333, "bc1qoriginalwallet",
                                   "rig01", "x", false, TM_POOL_PROTO_STRATUM_V1);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_set(0, &seed));
    TEST_ASSERT_TRUE(tm_pool_config_is_configured(0));

    tm_pool_cfg_t attempt = make_cfg("updated.example.com", 3333, "bc1qupdatedwallet",
                                      "rig01", "x", false, TM_POOL_PROTO_STRATUM_V1);
    fake_nvs_backend_fail_set_key("pool0_wallet");
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_config_set(0, &attempt));

    tm_pool_cfg_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_config_get(0, &out));
    /* Cross-wired: new host paired with the OLD wallet. */
    TEST_ASSERT_EQUAL_STRING("updated.example.com", out.host);
    TEST_ASSERT_EQUAL_STRING("bc1qoriginalwallet", out.wallet);
    /* Still reads as "configured" -- host/port/wallet/worker are all
     * non-empty/non-zero, even though wallet is stale relative to host. */
    TEST_ASSERT_TRUE(tm_pool_config_is_configured(0));
}
