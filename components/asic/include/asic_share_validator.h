#pragma once
#include <stdint.h>
#include "work.h"  // mining_work_t

typedef enum {
    ASIC_SHARE_OK,
    ASIC_SHARE_BELOW_TARGET,      // hash > target — silent (normal miss)
    ASIC_SHARE_INVALID_TARGET,    // is_target_valid() failed
    ASIC_SHARE_LOW_DIFFICULTY,    // share_diff < pool_diff / 2 sanity
} asic_share_verdict_t;

// Validate a candidate share. Pure: no HW, no RTOS, no globals.
// On ASIC_SHARE_OK: out_share_difficulty and out_hash[32] are populated.
// On any other verdict: outputs are unspecified (caller must ignore).
// NULL work / NULL out_share_difficulty / NULL out_hash → ASIC_SHARE_INVALID_TARGET.
asic_share_verdict_t asic_share_validate(
    const mining_work_t *work,
    uint32_t            nonce,
    uint32_t            version_bits,
    double             *out_share_difficulty,
    uint8_t             out_hash[32]);
