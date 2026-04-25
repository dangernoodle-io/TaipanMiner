#include "partition_fixup_decision.h"
#include <string.h>

partition_fixup_decision_t partition_fixup_decide(
    const uint8_t *expected_table, size_t expected_len,
    bool live_table_ok,
    const uint8_t *live_table,
    uint32_t running_addr,
    uint32_t expected_ota_0_addr)
{
    partition_fixup_decision_t d = {
        .action = PARTITION_FIXUP_SKIP_NO_TABLE,
        .needs_app_copy = false,
        .needs_table_rewrite = false,
    };
    if (!expected_table || expected_len <= 1) return d;
    if (!live_table_ok || !live_table) return d;  // can't determine, skip

    if (memcmp(live_table, expected_table, expected_len) == 0) {
        d.action = PARTITION_FIXUP_SKIP_TABLE_MATCHES;
        return d;
    }

    d.needs_table_rewrite = true;
    d.needs_app_copy = (running_addr != expected_ota_0_addr);
    d.action = d.needs_app_copy ? PARTITION_FIXUP_NEEDS_COPY_AND_REWRITE
                                 : PARTITION_FIXUP_NEEDS_REWRITE_ONLY;
    return d;
}
