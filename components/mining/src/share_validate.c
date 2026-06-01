#include "share_validate.h"
#include "work.h"
#include "sha256.h"
#include "bb_byte_order.h"
#include <string.h>

share_verdict_t share_validate(
    const mining_work_t *work,
    const uint8_t        hash[32],
    double              *out_diff)
{
    if (!work || !out_diff) {
        return SHARE_INVALID_TARGET;
    }

    // 1. Target sanity — must come BEFORE meets_target to avoid operating on a corrupt target.
    if (!is_target_valid(work->target)) {
        return SHARE_INVALID_TARGET;
    }

    // 2. Degenerate pool difficulty — catches misconfigured stratum connections.
    if (work->difficulty < 0.001) {
        return SHARE_INVALID_TARGET;
    }

    // 3. Hash vs target comparison.
    if (!meets_target(hash, work->target)) {
        return SHARE_BELOW_TARGET;
    }

    // 4. Compute share difficulty.
    *out_diff = hash_to_difficulty(hash);

    // 5. Pool-diff floor: share_diff must be at least half the pool difficulty.
    if (*out_diff < work->difficulty * 0.5) {
        return SHARE_LOW_DIFFICULTY;
    }

    return SHARE_VALID;
}

bool share_reverify(const mining_work_t *work, uint32_t ver_bits,
                    uint32_t nonce, const uint8_t claimed[32])
{
    uint8_t hdr[80];
    memcpy(hdr, work->header, 80);
    uint32_t mask = work->version_mask;
    if (mask) {
        uint32_t rolled = (work->version & ~mask) | (ver_bits & mask);
        bb_store_le32(hdr, rolled);   // header bytes 0..3, version LE
    }
    set_header_nonce(hdr, nonce);     // header bytes 76..79 LE
    uint8_t sw[32];
    sha256d(hdr, 80, sw);             // double-SHA256 into 32 bytes
    return memcmp(sw, claimed, 32) == 0;
}

bool share_meets_network_target(const uint8_t hash[32], uint32_t nbits)
{
    /* nbits_to_target() produces a big-endian target (MSB at index 0).
     * meets_target() and the hash argument both use Bitcoin's internal
     * little-endian convention (LSB at index 0, MSB at index 31).
     * Reverse the target before comparing. */
    uint8_t be_target[32];
    nbits_to_target(nbits, be_target);
    uint8_t le_target[32];
    for (int i = 0; i < 32; i++) {
        le_target[i] = be_target[31 - i];
    }
    return meets_target(hash, le_target);
}
