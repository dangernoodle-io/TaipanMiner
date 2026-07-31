#include "work_build.h"
#include "sha256.h"
#include "bb_byte_order.h"
#include "bb_str.h"
#include <string.h>
#include <math.h>

void build_coinbase_hash(const uint8_t *coinb1, size_t coinb1_len,
                         const uint8_t *extranonce1, size_t en1_len,
                         const uint8_t *extranonce2, size_t en2_len,
                         const uint8_t *coinb2, size_t coinb2_len,
                         uint8_t hash[32]) {
    // Total size of coinbase
    size_t total = coinb1_len + en1_len + en2_len + coinb2_len;

    // Create buffer for the full coinbase
    static uint8_t coinbase[1024];  // Reasonable max for coinbase
    if (total > sizeof(coinbase)) {
        // Error: coinbase too large. For now, just truncate or zero out hash.
        memset(hash, 0, 32);
        return;
    }

    // Concatenate all parts
    size_t offset = 0;
    memcpy(coinbase + offset, coinb1, coinb1_len);
    offset += coinb1_len;
    memcpy(coinbase + offset, extranonce1, en1_len);
    offset += en1_len;
    memcpy(coinbase + offset, extranonce2, en2_len);
    offset += en2_len;
    memcpy(coinbase + offset, coinb2, coinb2_len);
    offset += coinb2_len;

    // SHA256d of the entire coinbase
    sha256d(coinbase, total, hash);
}

void build_merkle_root(const uint8_t coinbase_hash[32],
                       const uint8_t branches[][32], size_t branch_count,
                       uint8_t root[32]) {
    // Start with coinbase hash
    uint8_t current[32];
    memcpy(current, coinbase_hash, 32);

    // For each branch, concatenate current + branch and SHA256d
    for (size_t i = 0; i < branch_count; i++) {
        uint8_t concat[64];
        memcpy(concat, current, 32);
        memcpy(concat + 32, branches[i], 32);
        sha256d(concat, 64, current);
    }

    // Copy final root
    memcpy(root, current, 32);
}

void serialize_header(uint32_t version, const uint8_t prevhash[32],
                      const uint8_t merkle_root[32], uint32_t ntime,
                      uint32_t nbits, uint32_t nonce,
                      uint8_t header[80]) {
    // Bytes 0-3: version (uint32 LE)
    bb_store_le32(header, version);

    // Bytes 4-35: prevhash (32 bytes, already in correct byte order)
    memcpy(header + 4, prevhash, 32);

    // Bytes 36-67: merkle_root (32 bytes)
    memcpy(header + 36, merkle_root, 32);

    // Bytes 68-71: ntime (uint32 LE)
    bb_store_le32(header + 68, ntime);

    // Bytes 72-75: nbits (uint32 LE)
    bb_store_le32(header + 72, nbits);

    // Bytes 76-79: nonce (uint32 LE)
    bb_store_le32(header + 76, nonce);
}

void decode_stratum_prevhash(const char *hex, uint8_t prevhash[32]) {
    // Stratum sends prevhash as 64 hex chars = 32 bytes
    // It's organized as 8 groups of 4 bytes (8 hex chars each)
    // Each group has its bytes reversed

    // First, convert hex to raw bytes. bb_str_hex_to_bytes stops decoding at
    // the first non-hex character and leaves any remaining bytes untouched,
    // so zero-init here to get deterministic zero-padding on short/malformed
    // input. Rejecting malformed jobs outright is stratum's job (TA-560);
    // this function only guarantees a deterministic decode.
    uint8_t raw[32] = {0};
    bb_str_hex_to_bytes(hex, raw, 32);

    // Now reverse bytes within each 4-byte group
    for (int i = 0; i < 8; i++) {
        int group_start = i * 4;
        prevhash[group_start] = raw[group_start + 3];
        prevhash[group_start + 1] = raw[group_start + 2];
        prevhash[group_start + 2] = raw[group_start + 1];
        prevhash[group_start + 3] = raw[group_start];
    }
}

void difficulty_to_target(double diff, uint8_t target[32])
{
    memset(target, 0, 32);
    if (!isfinite(diff) || diff < 0.001) {
        diff = 0.001;
    }

    // Bitcoin LE convention: byte[0]=LSB, byte[31]=MSB
    // diff1 target has 0xFFFF at BE bytes 4-5, which is LE bytes 26-27
    double val = 65535.0 / diff;

    // Integer part: right-aligned at LE byte 26 (BE byte 5 → LE byte 26)
    uint64_t iv = (val < (double)UINT64_MAX) ? (uint64_t)val : UINT64_MAX;
    for (int i = 26; i <= 31 && iv > 0; i++) {
        target[i] = (uint8_t)(iv & 0xFF);
        iv >>= 8;
    }

    // Fractional part: LE bytes 25 down (BE bytes 6+ → LE bytes 25-)
    double frac = val - floor(val);
    for (int i = 25; i >= 20 && frac > 0.0; i--) {
        frac *= 256.0;
        uint8_t b = (uint8_t)frac;
        target[i] = b;
        frac -= b;
    }
}
