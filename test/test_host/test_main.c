// Host test harness entry point — PR 1 skeleton has no host-testable
// module yet (src/main.c is ESP-IDF-only composition). Later PRs add
// test_<module>_<behavior> functions here (or in sibling files) and
// RUN_TEST() them manually below; no auto-discovery.
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    return UNITY_END();
}
