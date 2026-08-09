// TA-561: mining_gather() purity + descriptor correctness tests.
//
// mining_gather() is the ENTIRE delivery-facing surface of the mining
// engine — pure (no I/O, no allocation, no delivery knowledge), fills a
// mining_snap_t from live state. On host there is no live mining_stats
// (no FreeRTOS mutex, mining task never runs), so tests seed the host-only
// mirror via mining_gather_set_snapshot_for_test() and assert mining_gather()
// round-trips it faithfully.

#include "unity.h"
#include "mining.h"
#include "bb_serialize.h"
#include <string.h>

void test_mining_gather_unseeded_is_zeroed(void)
{
    mining_gather_set_snapshot_for_test(NULL);
    mining_snap_t snap;
    memset(&snap, 0xAA, sizeof(snap));  // poison to prove gather actually zeroes it
    mining_gather(NULL, &snap);

    TEST_ASSERT_EQUAL_UINT64(0, snap.hashrate_hs);
    TEST_ASSERT_EQUAL_UINT64(0, snap.accepted);
    TEST_ASSERT_EQUAL_UINT64(0, snap.rejected);
    TEST_ASSERT_EQUAL_UINT64(0, snap.hashes);
    TEST_ASSERT_EQUAL_UINT64(0, snap.blocks_found);
    TEST_ASSERT_FALSE(snap.sha_self_test_failed);
}

void test_mining_gather_round_trips_seeded_snapshot(void)
{
    mining_snap_t in = {
        .hashrate_hs = 223456,
        .hashrate_1m_hs = 220000.5,
        .hashrate_10m_hs = 219000.25,
        .hashrate_1h_hs = 218500.125,
        .accepted = 42,
        .rejected = 3,
        .best_diff = 1.234,
        .hashes = 9876543210ULL,
        .blocks_found = 0,
        .temp_c = 41.5,
        .uptime_s = 3600,
        .sha_self_test_failed = false,
    };
    mining_gather_set_snapshot_for_test(&in);

    mining_snap_t out;
    memset(&out, 0, sizeof(out));
    mining_gather(NULL, &out);

    TEST_ASSERT_EQUAL_UINT64(in.hashrate_hs, out.hashrate_hs);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, in.hashrate_1m_hs, out.hashrate_1m_hs);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, in.hashrate_10m_hs, out.hashrate_10m_hs);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, in.hashrate_1h_hs, out.hashrate_1h_hs);
    TEST_ASSERT_EQUAL_UINT64(in.accepted, out.accepted);
    TEST_ASSERT_EQUAL_UINT64(in.rejected, out.rejected);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, in.best_diff, out.best_diff);
    TEST_ASSERT_EQUAL_UINT64(in.hashes, out.hashes);
    TEST_ASSERT_EQUAL_UINT64(in.blocks_found, out.blocks_found);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, in.temp_c, out.temp_c);
    TEST_ASSERT_EQUAL_INT64(in.uptime_s, out.uptime_s);
    TEST_ASSERT_FALSE(out.sha_self_test_failed);

    mining_gather_set_snapshot_for_test(NULL);
}

// Companion to test_mining_gather_round_trips_seeded_snapshot -- that test
// only ever seeds sha_self_test_failed=false (its `in` initializer omits
// the field, defaulting it), leaving the true branch of the round-trip
// unexercised.
void test_mining_gather_round_trips_sha_self_test_failed_true(void)
{
    mining_snap_t in = {
        .hashrate_hs = 100,
        .accepted = 1,
        .rejected = 0,
        .hashes = 100,
        .blocks_found = 0,
        .uptime_s = 60,
        .sha_self_test_failed = true,
    };
    mining_gather_set_snapshot_for_test(&in);

    mining_snap_t out;
    memset(&out, 0, sizeof(out));
    mining_gather(NULL, &out);

    TEST_ASSERT_TRUE(out.sha_self_test_failed);

    mining_gather_set_snapshot_for_test(NULL);
}

void test_mining_gather_null_out_is_safe(void)
{
    mining_gather(NULL, NULL);  // must not crash
    TEST_ASSERT_TRUE(true);
}

// Descriptor sanity: every field's offset must be within the struct, and the
// field count / snap_size must match the actual C type — the exact
// silent-OOB-read hazard the bb_serialize walker is documented to have if a
// field's declared bb_type_t doesn't match its physical storage.
void test_mining_desc_snap_size_matches_struct(void)
{
    TEST_ASSERT_EQUAL_UINT16(sizeof(mining_snap_t), mining_desc.snap_size);
    TEST_ASSERT_TRUE(mining_desc.n_fields > 0);
    for (uint16_t i = 0; i < mining_desc.n_fields; i++) {
        TEST_ASSERT_TRUE(mining_desc.fields[i].offset < mining_desc.snap_size);
    }
}

void test_mining_desc_find_known_key(void)
{
    const bb_serialize_field_t *f = bb_serialize_desc_find(&mining_desc, "hashrate_hs");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(BB_TYPE_U64, f->type);
}

void test_mining_desc_find_unknown_key(void)
{
    TEST_ASSERT_NULL(bb_serialize_desc_find(&mining_desc, "no-such-field"));
}

// ---------------------------------------------------------------------------
// Walk-based round-trip: drives bb_serialize_walk() over mining_desc with a
// minimal capturing emit vtable and asserts the emitted field values match
// a seeded mining_snap_t. This catches a real offset/type mismatch between
// s_mining_fields and mining_snap_t mechanically -- the size/find checks
// above only validate the descriptor's own bookkeeping (snap_size, offset
// bounds, key lookup), not that the walker actually reads the right bytes
// at the right offset for the right C type.
// ---------------------------------------------------------------------------

// Captures every field the walker emits, keyed by name -- mining_snap_t is
// entirely flat scalars, so no begin_obj/begin_arr is expected.
typedef struct {
    uint64_t hashrate_hs;
    double   hashrate_1m_hs;
    double   hashrate_10m_hs;
    double   hashrate_1h_hs;
    uint64_t accepted;
    uint64_t rejected;
    double   best_diff;
    uint64_t hashes;
    uint64_t blocks_found;
    double   temp_c;
    int64_t  uptime_s;
    bool     sha_self_test_failed;
} s_captured_t;

static s_captured_t s_captured;

static void s_cap_begin_obj(void *ctx, const char *key) { (void)ctx; (void)key; TEST_FAIL_MESSAGE("unexpected begin_obj"); }
static void s_cap_end_obj(void *ctx) { (void)ctx; TEST_FAIL_MESSAGE("unexpected end_obj"); }
static void s_cap_begin_arr(void *ctx, const char *key) { (void)ctx; (void)key; TEST_FAIL_MESSAGE("unexpected begin_arr"); }
static void s_cap_end_arr(void *ctx) { (void)ctx; TEST_FAIL_MESSAGE("unexpected end_arr"); }
static void s_cap_emit_str(void *ctx, const char *key, const char *s, size_t len) { (void)ctx; (void)key; (void)s; (void)len; TEST_FAIL_MESSAGE("unexpected emit_str"); }
static void s_cap_emit_null(void *ctx, const char *key) { (void)ctx; (void)key; TEST_FAIL_MESSAGE("unexpected emit_null"); }

static void s_cap_emit_u64(void *ctx, const char *key, uint64_t v)
{
    (void)ctx;
    if (strcmp(key, "hashrate_hs") == 0)        s_captured.hashrate_hs = v;
    else if (strcmp(key, "accepted") == 0)      s_captured.accepted = v;
    else if (strcmp(key, "rejected") == 0)      s_captured.rejected = v;
    else if (strcmp(key, "hashes") == 0)        s_captured.hashes = v;
    else if (strcmp(key, "blocks_found") == 0)  s_captured.blocks_found = v;
    else TEST_FAIL_MESSAGE("unexpected emit_u64 key");
}

static void s_cap_emit_f64(void *ctx, const char *key, double v)
{
    (void)ctx;
    if (strcmp(key, "hashrate_1m_hs") == 0)       s_captured.hashrate_1m_hs = v;
    else if (strcmp(key, "hashrate_10m_hs") == 0) s_captured.hashrate_10m_hs = v;
    else if (strcmp(key, "hashrate_1h_hs") == 0)  s_captured.hashrate_1h_hs = v;
    else if (strcmp(key, "best_diff") == 0)       s_captured.best_diff = v;
    else if (strcmp(key, "temp_c") == 0)          s_captured.temp_c = v;
    else TEST_FAIL_MESSAGE("unexpected emit_f64 key");
}

static void s_cap_emit_i64(void *ctx, const char *key, int64_t v)
{
    (void)ctx;
    if (strcmp(key, "uptime_s") == 0) s_captured.uptime_s = v;
    else TEST_FAIL_MESSAGE("unexpected emit_i64 key");
}

static void s_cap_emit_bool(void *ctx, const char *key, bool v)
{
    (void)ctx;
    if (strcmp(key, "sha_self_test_failed") == 0) s_captured.sha_self_test_failed = v;
    else TEST_FAIL_MESSAGE("unexpected emit_bool key");
}

static const bb_serialize_emit_t s_cap_emit = {
    .format_id = BB_FORMAT_NONE,
    .ctx = NULL,
    .begin_obj = s_cap_begin_obj,
    .end_obj = s_cap_end_obj,
    .begin_arr = s_cap_begin_arr,
    .end_arr = s_cap_end_arr,
    .emit_i64 = s_cap_emit_i64,
    .emit_u64 = s_cap_emit_u64,
    .emit_f64 = s_cap_emit_f64,
    .emit_bool = s_cap_emit_bool,
    .emit_str = s_cap_emit_str,
    .emit_null = s_cap_emit_null,
};

void test_mining_desc_walk_matches_seeded_snapshot(void)
{
    mining_snap_t seed = {
        .hashrate_hs = 223456,
        .hashrate_1m_hs = 220000.5,
        .hashrate_10m_hs = 219000.25,
        .hashrate_1h_hs = 218500.125,
        .accepted = 42,
        .rejected = 3,
        .best_diff = 1.234,
        .hashes = 9876543210ULL,
        .blocks_found = 7,
        .temp_c = 41.5,
        .uptime_s = 3600,
        .sha_self_test_failed = true,
    };
    memset(&s_captured, 0, sizeof(s_captured));

    bb_serialize_walk(&(bb_serialize_walk_cfg_t){
        .desc = &mining_desc,
        .snap = &seed,
        .emit = &s_cap_emit,
    });

    TEST_ASSERT_EQUAL_UINT64(seed.hashrate_hs, s_captured.hashrate_hs);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, seed.hashrate_1m_hs, s_captured.hashrate_1m_hs);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, seed.hashrate_10m_hs, s_captured.hashrate_10m_hs);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, seed.hashrate_1h_hs, s_captured.hashrate_1h_hs);
    TEST_ASSERT_EQUAL_UINT64(seed.accepted, s_captured.accepted);
    TEST_ASSERT_EQUAL_UINT64(seed.rejected, s_captured.rejected);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, seed.best_diff, s_captured.best_diff);
    TEST_ASSERT_EQUAL_UINT64(seed.hashes, s_captured.hashes);
    TEST_ASSERT_EQUAL_UINT64(seed.blocks_found, s_captured.blocks_found);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, seed.temp_c, s_captured.temp_c);
    TEST_ASSERT_EQUAL_INT64(seed.uptime_s, s_captured.uptime_s);
    TEST_ASSERT_TRUE(s_captured.sha_self_test_failed);
}
