#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PARTITION_FIXUP_SKIP_NO_TABLE = 0,     // stub build / embedded table empty
    PARTITION_FIXUP_SKIP_TABLE_MATCHES,    // already correct
    PARTITION_FIXUP_NEEDS_REWRITE_ONLY,    // table wrong but running at correct ota_0
    PARTITION_FIXUP_NEEDS_COPY_AND_REWRITE, // table wrong AND running from different offset
} partition_fixup_action_t;

typedef struct {
    partition_fixup_action_t action;
    bool needs_app_copy;       // running != expected_ota_0_addr
    bool needs_table_rewrite;  // table differs from expected
} partition_fixup_decision_t;

// Decide whether and how to fix up the partition layout.
//
// expected_table: bytes of the embedded partitions.bin (or NULL/0 for stub).
// live_table_ok: whether reading the current live table succeeded.
// live_table: bytes read from flash (only valid if live_table_ok).
// running_addr: where the running app actually sits (esp_ota_get_running_partition).
// expected_ota_0_addr: where ota_0 should sit per our partition layout (typically 0x20000).
//
// Pure: no flash I/O, no logging.
partition_fixup_decision_t partition_fixup_decide(
    const uint8_t *expected_table, size_t expected_len,
    bool live_table_ok,
    const uint8_t *live_table,
    uint32_t running_addr,
    uint32_t expected_ota_0_addr);
