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
