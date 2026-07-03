#include "unity.h"
#include "stratum_health.h"
#include "bb_transport_health.h"

#include <string.h>

// B1-684: stratum connection state feeds bb_transport_health's SSOT.

static bool find_slot(const char *name, bb_transport_health_snapshot_t *out)
{
    bb_transport_health_snapshot_t s[BB_TRANSPORT_HEALTH_MAX_SLOTS];
    size_t n = bb_transport_health_snapshot_all(s, BB_TRANSPORT_HEALTH_MAX_SLOTS);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(s[i].name, name) == 0) {
            *out = s[i];
            return true;
        }
    }
    return false;
}

void test_stratum_health_authoritative_tracks_up_down(void)
{
    bb_transport_health_reset_for_test();
    stratum_health_reset_for_test();

    bb_transport_health_snapshot_t slot;
    int en, fail;

    // seed down — registers as AUTHORITATIVE, failing
    stratum_health_report(false);
    TEST_ASSERT_TRUE(find_slot("stratum", &slot));
    TEST_ASSERT_EQUAL_INT(BB_TRANSPORT_AUTHORITATIVE, slot.cls);
    TEST_ASSERT_TRUE(slot.failing);
    bb_transport_health_authoritative_counts(&en, &fail);
    TEST_ASSERT_EQUAL_INT(1, en);
    TEST_ASSERT_EQUAL_INT(1, fail);

    // up — failing clears
    stratum_health_report(true);
    bb_transport_health_authoritative_counts(&en, &fail);
    TEST_ASSERT_EQUAL_INT(1, en);
    TEST_ASSERT_EQUAL_INT(0, fail);

    // down again — failing set
    stratum_health_report(false);
    bb_transport_health_authoritative_counts(&en, &fail);
    TEST_ASSERT_EQUAL_INT(1, fail);

    // register-once: repeated report() calls never create a duplicate slot
    bb_transport_health_snapshot_t s[BB_TRANSPORT_HEALTH_MAX_SLOTS];
    size_t n = bb_transport_health_snapshot_all(s, BB_TRANSPORT_HEALTH_MAX_SLOTS);
    int count = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(s[i].name, "stratum") == 0) count++;
    }
    TEST_ASSERT_EQUAL_INT(1, count);
}

void test_stratum_health_register_while_up(void)
{
    bb_transport_health_reset_for_test();
    stratum_health_reset_for_test();

    bb_transport_health_snapshot_t slot;
    int en, fail;

    // register-while-up: calling stratum_health_report(true) as first report
    // (without prior seed=false) registers the slot and it is NOT failing
    stratum_health_report(true);
    TEST_ASSERT_TRUE(find_slot("stratum", &slot));
    TEST_ASSERT_EQUAL_INT(BB_TRANSPORT_AUTHORITATIVE, slot.cls);
    TEST_ASSERT_FALSE(slot.failing);
    bb_transport_health_authoritative_counts(&en, &fail);
    TEST_ASSERT_EQUAL_INT(1, en);
    TEST_ASSERT_EQUAL_INT(0, fail);
}
