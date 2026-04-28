#include "asic_share_validator.h"
#include "work.h"
#include "sha256.h"
#include <string.h>

asic_share_verdict_t asic_share_validate(
    const mining_work_t *work,
    uint32_t            nonce,
    uint32_t            version_bits,
    double             *out_share_difficulty,
    uint8_t             out_hash[32])
{
    if (!work || !out_share_difficulty || !out_hash) {
        return ASIC_SHARE_INVALID_TARGET;
    }

    uint8_t header_copy[80];
    memcpy(header_copy, work->header, 80);

    // Apply version rolling (LE in header).
    // Mirror asic_task.c exactly: gate on both ver_bits != 0 AND version_mask != 0.
    if (version_bits != 0 && work->version_mask != 0) {
        uint32_t rolled = (work->version & ~work->version_mask) | (version_bits & work->version_mask);
        header_copy[0] = (uint8_t)(rolled);
        header_copy[1] = (uint8_t)(rolled >> 8);
        header_copy[2] = (uint8_t)(rolled >> 16);
        header_copy[3] = (uint8_t)(rolled >> 24);
    }

    // Apply nonce at bytes 76–79 LE.
    // nonce is the LE uint32 (nonce.nonce[0] | nonce.nonce[1]<<8 | ...) from the wire bytes,
    // so writing it LE reconstructs the exact bytes the ASIC produced.
    header_copy[76] = (uint8_t)(nonce);
    header_copy[77] = (uint8_t)(nonce >> 8);
    header_copy[78] = (uint8_t)(nonce >> 16);
    header_copy[79] = (uint8_t)(nonce >> 24);

    sha256d(header_copy, 80, out_hash);

    if (!meets_target(out_hash, work->target)) {
        return ASIC_SHARE_BELOW_TARGET;
    }

    if (!is_target_valid(work->target)) {
        return ASIC_SHARE_INVALID_TARGET;
    }

    *out_share_difficulty = hash_to_difficulty(out_hash);

    if (*out_share_difficulty < work->difficulty / 2.0) {
        return ASIC_SHARE_LOW_DIFFICULTY;
    }

    return ASIC_SHARE_OK;
}
