#include "unity.h"
#include "partition_fixup_decision.h"
#include <string.h>

static const uint8_t SAMPLE_TABLE[] = "EXPECTED_TABLE_BYTES";
#define EXPECTED_OTA_0_ADDR 0x20000
#define WRONG_RUNNING_ADDR  0x10000

void test_pfd_skip_when_expected_empty(void) {
    partition_fixup_decision_t d = partition_fixup_decide(
        NULL, 0, true, SAMPLE_TABLE, 0x20000, EXPECTED_OTA_0_ADDR);
    TEST_ASSERT_EQUAL(PARTITION_FIXUP_SKIP_NO_TABLE, d.action);
    TEST_ASSERT_FALSE(d.needs_app_copy);
    TEST_ASSERT_FALSE(d.needs_table_rewrite);
}

void test_pfd_skip_when_expected_too_short(void) {
    uint8_t one_byte = 0;
    partition_fixup_decision_t d = partition_fixup_decide(
        &one_byte, 1, true, SAMPLE_TABLE, 0x20000, EXPECTED_OTA_0_ADDR);
    TEST_ASSERT_EQUAL(PARTITION_FIXUP_SKIP_NO_TABLE, d.action);
    TEST_ASSERT_FALSE(d.needs_app_copy);
    TEST_ASSERT_FALSE(d.needs_table_rewrite);
}

void test_pfd_skip_when_live_unreadable(void) {
    partition_fixup_decision_t d = partition_fixup_decide(
        SAMPLE_TABLE, sizeof(SAMPLE_TABLE), false, NULL, 0x20000, EXPECTED_OTA_0_ADDR);
    TEST_ASSERT_EQUAL(PARTITION_FIXUP_SKIP_NO_TABLE, d.action);
    TEST_ASSERT_FALSE(d.needs_app_copy);
    TEST_ASSERT_FALSE(d.needs_table_rewrite);
}

void test_pfd_skip_when_table_matches(void) {
    partition_fixup_decision_t d = partition_fixup_decide(
        SAMPLE_TABLE, sizeof(SAMPLE_TABLE), true, SAMPLE_TABLE,
        EXPECTED_OTA_0_ADDR, EXPECTED_OTA_0_ADDR);
    TEST_ASSERT_EQUAL(PARTITION_FIXUP_SKIP_TABLE_MATCHES, d.action);
    TEST_ASSERT_FALSE(d.needs_app_copy);
    TEST_ASSERT_FALSE(d.needs_table_rewrite);
}

void test_pfd_rewrite_only_when_running_at_correct_addr(void) {
    uint8_t different[] = "DIFFERENT_TABLE_BYTES";  // same length as SAMPLE_TABLE, different content
    partition_fixup_decision_t d = partition_fixup_decide(
        SAMPLE_TABLE, sizeof(SAMPLE_TABLE), true, different,
        EXPECTED_OTA_0_ADDR, EXPECTED_OTA_0_ADDR);
    TEST_ASSERT_EQUAL(PARTITION_FIXUP_NEEDS_REWRITE_ONLY, d.action);
    TEST_ASSERT_FALSE(d.needs_app_copy);
    TEST_ASSERT_TRUE(d.needs_table_rewrite);
}

void test_pfd_copy_and_rewrite_when_running_at_wrong_addr(void) {
    uint8_t different[] = "DIFFERENT_TABLE_BYTES";
    partition_fixup_decision_t d = partition_fixup_decide(
        SAMPLE_TABLE, sizeof(SAMPLE_TABLE), true, different,
        WRONG_RUNNING_ADDR, EXPECTED_OTA_0_ADDR);
    TEST_ASSERT_EQUAL(PARTITION_FIXUP_NEEDS_COPY_AND_REWRITE, d.action);
    TEST_ASSERT_TRUE(d.needs_app_copy);
    TEST_ASSERT_TRUE(d.needs_table_rewrite);
}

void test_pfd_running_addr_zero_treated_as_wrong(void) {
    /* Defensive: if esp_ota_get_running_partition returned NULL, caller passes 0. */
    uint8_t different[] = "DIFFERENT_TABLE_BYTES";
    partition_fixup_decision_t d = partition_fixup_decide(
        SAMPLE_TABLE, sizeof(SAMPLE_TABLE), true, different,
        0, EXPECTED_OTA_0_ADDR);
    TEST_ASSERT_EQUAL(PARTITION_FIXUP_NEEDS_COPY_AND_REWRITE, d.action);
    TEST_ASSERT_TRUE(d.needs_app_copy);
    TEST_ASSERT_TRUE(d.needs_table_rewrite);
}
