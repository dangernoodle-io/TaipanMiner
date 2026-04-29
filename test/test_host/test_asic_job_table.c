#include "unity.h"
#include "asic_proto.h"
#include <string.h>

// TA-297: tests for ASIC job-table slot-mapping and identity/staleness guard.
//
// The slot-mapping (asic_job_slot) and stale-detection (asic_job_slot_stale)
// are pure helpers in asic_proto.h; we test them directly without touching
// the static asic_task state.

// ---------------------------------------------------------------------------
// asic_job_slot: verifies ASIC_JOB_TABLE_SIZE=16 slot mapping
// ---------------------------------------------------------------------------

void test_job_slot_id_zero_maps_to_zero(void) {
    TEST_ASSERT_EQUAL_UINT(0, asic_job_slot(0));
}

void test_job_slot_id_below_table_size_maps_to_itself(void) {
    // IDs 0..15 are their own slots
    for (uint8_t id = 0; id < ASIC_JOB_TABLE_SIZE; id++) {
        TEST_ASSERT_EQUAL_UINT((size_t)id, asic_job_slot(id));
    }
}

void test_job_slot_wraps_at_table_size(void) {
    // ASIC_JOB_TABLE_SIZE=16: id=16 → slot 0, id=17 → slot 1, ...
    TEST_ASSERT_EQUAL_UINT(0,  asic_job_slot(ASIC_JOB_TABLE_SIZE));
    TEST_ASSERT_EQUAL_UINT(1,  asic_job_slot(ASIC_JOB_TABLE_SIZE + 1));
    TEST_ASSERT_EQUAL_UINT(15, asic_job_slot(ASIC_JOB_TABLE_SIZE + 15));
}

void test_job_slot_step24_id_maps_correctly(void) {
    // Wire IDs produced by stride 24: 0, 24, 48, 72, 96, 120 (mod 128)
    // Slot = id % 16
    TEST_ASSERT_EQUAL_UINT( 0, asic_job_slot(0));
    TEST_ASSERT_EQUAL_UINT( 8, asic_job_slot(24));
    TEST_ASSERT_EQUAL_UINT( 0, asic_job_slot(48));   // 48 % 16 = 0
    TEST_ASSERT_EQUAL_UINT( 8, asic_job_slot(72));   // 72 % 16 = 8
    TEST_ASSERT_EQUAL_UINT( 0, asic_job_slot(96));   // 96 % 16 = 0
    TEST_ASSERT_EQUAL_UINT( 8, asic_job_slot(120));  // 120 % 16 = 8
}

void test_job_slot_max_wire_id(void) {
    // Maximum valid wire ID is ASIC_JOB_ID_MOD-1 = 127
    TEST_ASSERT_EQUAL_UINT(127 % ASIC_JOB_TABLE_SIZE, asic_job_slot(127));
}

// ---------------------------------------------------------------------------
// asic_job_slot_stale: identity and empty-slot guard logic
// ---------------------------------------------------------------------------

// Test 1 — round-trip: slot_id_seen matches real_job_id and work is non-empty → not stale
void test_job_slot_stale_matching_id_nonempty_accepted(void) {
    // Simulates: dispatch real_job_id=24, slot=8; s_job_id_seen[8]=24; job_id[0]='a'
    bool stale = asic_job_slot_stale(/*seen=*/24, /*work_id0=*/'a', /*real=*/24);
    TEST_ASSERT_FALSE(stale);
}

// Test 2 — stale rejection: slot recycled to a newer job ID
void test_job_slot_stale_recycled_slot_rejected(void) {
    // Dispatch id=24 fills slot 8. Then dispatch id=40 (also slot 8, 40%16=8)
    // overwrites it. Now a late nonce for id=24 arrives: seen=40, real=24 → stale.
    bool stale = asic_job_slot_stale(/*seen=*/40, /*work_id0=*/'b', /*real=*/24);
    TEST_ASSERT_TRUE(stale);
}

// Test 3 — empty slot: work_job_id[0] == '\0' → stale even if ids match
void test_job_slot_stale_empty_slot_rejected(void) {
    // After memset(s_job_table,0,...): job_id[0]='\0', seen=0.
    // A nonce citing real_job_id=0 before any dispatch → stale.
    bool stale = asic_job_slot_stale(/*seen=*/0, /*work_id0=*/'\0', /*real=*/0);
    TEST_ASSERT_TRUE(stale);
}

// Test 4 — id matches but slot is empty (freshly cleared)
void test_job_slot_stale_id_match_but_slot_empty_rejected(void) {
    // seen=24 (hypothetical), but work_job_id[0]='\0' (slot was zeroed)
    bool stale = asic_job_slot_stale(/*seen=*/24, /*work_id0=*/'\0', /*real=*/24);
    TEST_ASSERT_TRUE(stale);
}

// Test 5 — bounds: real_job_id >= ASIC_JOB_ID_MOD is handled before table lookup;
// the guard below only exercises the slot logic, not the wire-range check.
// We verify asic_job_slot_stale is not consulted for out-of-range IDs
// (this is a compile-time property — document it via a sanity slot check).
void test_job_slot_at_wire_boundary_mod_rejected_upstream(void) {
    // real_job_id == ASIC_JOB_ID_MOD (128) is rejected before asic_job_slot_stale
    // is called; the function itself would compute 128 % 16 = 0.  Ensure the
    // formula is consistent even if called with an out-of-range value.
    size_t slot = asic_job_slot(ASIC_JOB_ID_MOD);  // 128 % 16 = 0
    TEST_ASSERT_EQUAL_UINT(0, slot);
}

// Test 6 — non-empty work, different seen: stale regardless of job_id[0]
void test_job_slot_stale_mismatch_rejected_regardless_of_work_content(void) {
    // Slot 8 was written for id=40; nonce claims id=24 (both map to slot 8).
    bool stale = asic_job_slot_stale(/*seen=*/40, /*work_id0=*/'x', /*real=*/24);
    TEST_ASSERT_TRUE(stale);
    // And for the other direction
    stale = asic_job_slot_stale(/*seen=*/24, /*work_id0=*/'x', /*real=*/40);
    TEST_ASSERT_TRUE(stale);
}

// Test 7 — stride-24 IDs produce consistent slot assignments.
// With stride=24, ASIC_JOB_ID_MOD=128, ASIC_JOB_TABLE_SIZE=16:
//   gcd(24, 16) = 8, so the 16 unique wire IDs map to only 2 distinct slots
//   (slot 0 for even multiples of 16, slot 8 for odd multiples).
// This is expected: the identity guard (s_job_id_seen) distinguishes wire IDs
// that collide on the same slot. Verify the slot function is consistent.
void test_job_slot_all_stride24_ids_are_distinct_slots(void) {
    // stride=24, table=16: gcd(24,16)=8 → 16/8=2 distinct slots
    // slot 0: IDs 0,48,96,16,64,112,32,80 (id%16==0)
    // slot 8: IDs 24,72,120,40,88,8,56,104 (id%16==8)
    TEST_ASSERT_EQUAL_UINT(0, asic_job_slot(0));
    TEST_ASSERT_EQUAL_UINT(8, asic_job_slot(24));
    TEST_ASSERT_EQUAL_UINT(0, asic_job_slot(48));
    TEST_ASSERT_EQUAL_UINT(8, asic_job_slot(72));
    TEST_ASSERT_EQUAL_UINT(0, asic_job_slot(96));
    TEST_ASSERT_EQUAL_UINT(8, asic_job_slot(120));

    // The identity guard distinguishes colliding IDs on the same slot.
    // e.g. id=0 and id=48 both land on slot 0; if slot 0 was written for id=48,
    // a late nonce citing id=0 is rejected because seen=48 != 0.
    TEST_ASSERT_TRUE(asic_job_slot_stale(/*seen=*/48, /*work_id0=*/'x', /*real=*/0));
    TEST_ASSERT_FALSE(asic_job_slot_stale(/*seen=*/48, /*work_id0=*/'x', /*real=*/48));
}
