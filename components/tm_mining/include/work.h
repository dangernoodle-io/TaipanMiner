#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Mining work (ready to hash)
typedef struct {
    uint8_t  header[80];         // serialized block header
    uint8_t  target[32];         // 256-bit target, little-endian (byte[31]=MSB),
                                  // matches meets_target's convention -- reverse
                                  // nbits_to_target()'s big-endian output before
                                  // storing here (see share_meets_network_target)
    uint32_t version;            // for version rolling
    uint32_t version_mask;       // BIP 320: 0 if version rolling not active
    uint32_t ntime;              // for ntime rolling
    uint32_t nbits;              // compact target from stratum job (for network-target check)
    char     job_id[64];
    char     extranonce2_hex[17]; // extranonce2 as hex string (8 bytes = 16 hex chars + null)
    double   difficulty;         // pool difficulty
    bool     clean;              // true = new block, interrupt mining immediately
    uint32_t work_seq;           // sequence counter — incremented on each new work build
} mining_work_t;

// Set nonce in an already-serialized header (bytes 76-79)
void set_header_nonce(uint8_t header[80], uint32_t nonce);

// Convert nbits (compact target) to 256-bit target (big-endian)
void nbits_to_target(uint32_t nbits, uint8_t target[32]);

// Check if hash meets target (both 32-byte values, compared as big-endian 256-bit integers)
// Returns true if hash <= target
bool meets_target(const uint8_t hash[32], const uint8_t target[32]);

// Validate target sanity — rejects all-zero, all-0xFF, and targets with non-zero MSBs
bool is_target_valid(const uint8_t target[32]);

// Convert a verified 256-bit hash (big-endian) to pool difficulty
double hash_to_difficulty(const uint8_t hash[32]);

// Convert 8 canonical SHA-256 state words (host uint32_t, where state[0] is the
// most-significant word) into the 32-byte hash format used by meets_target /
// difficulty_to_target. Format: hash[i*4..i*4+3] = canonical state[i] stored
// big-endian (MSB first). Single source of truth across SW / HW SHA backends.
void mining_hash_from_state(const uint32_t state[8], uint8_t hash_out[32]);

// Iterate through BIP 320 version-rolling submask values.
// Start with ver_bits=0. Returns next ver_bits; 0 when exhausted.
static inline uint32_t next_version_roll(uint32_t ver_bits, uint32_t mask) {
    if (mask == 0) return 0;
    if (ver_bits == 0) return mask;
    uint32_t next = (ver_bits - 1) & mask;
    return next;  // 0 means exhausted
}

// Roll the BIP 320 version field into a serialized header's first 4 bytes
// (little-endian), if version rolling is active. Call unconditionally on
// EVERY outer-loop pass, INCLUDING the first ver_bits=0 pass -- the guard
// here must be `if (version_mask != 0)` only, never additionally gated on
// ver_bits != 0. s_process_prefilter_hit() (mining.c) unconditionally
// recomputes rolled = (version & ~mask) | (ver_bits & mask) whenever
// mask != 0, so the hashed header must match that on every pass, not just
// once ver_bits becomes nonzero. Skipping the ver_bits=0 write leaves the
// header holding the raw unmasked version, which disagrees with that
// masked-at-ver_bits=0 reconstruction and silently drops every real share
// whenever the pool's base version has a bit set inside version_mask.
static inline void roll_header_version(uint8_t header[80], uint32_t base_version,
                                        uint32_t version_mask, uint32_t ver_bits) {
    if (version_mask == 0) {
        return;
    }
    uint32_t rolled = (base_version & ~version_mask) | (ver_bits & version_mask);
    header[0] = rolled & 0xFF;
    header[1] = (rolled >> 8) & 0xFF;
    header[2] = (rolled >> 16) & 0xFF;
    header[3] = (rolled >> 24) & 0xFF;
}
