#pragma once
#include <stdint.h>
#include "work.h"  // mining_work_t

typedef enum {
    SHARE_VALID,
    SHARE_BELOW_TARGET,       // hash > target — silent (normal miss)
    SHARE_INVALID_TARGET,     // is_target_valid() failed or work->difficulty < 0.001
    SHARE_LOW_DIFFICULTY,     // share_diff < pool_diff / 2 sanity
} share_verdict_t;

// Validate a candidate share. Pure: no HW, no RTOS, no globals.
// Validation order:
//   1. is_target_valid(work->target)   — catches corrupt targets before any hash compare
//   2. work->difficulty < 0.001        — degenerate pool difficulty
//   3. meets_target(hash, work->target)
//   4. *out_diff = hash_to_difficulty(hash)
//   5. *out_diff < work->difficulty * 0.5
//
// On SHARE_VALID: *out_diff is populated with the computed share difficulty.
// On any other verdict: *out_diff is unspecified; caller must ignore.
// NULL work / NULL out_diff → SHARE_INVALID_TARGET.
share_verdict_t share_validate(
    const mining_work_t *work,
    const uint8_t        hash[32],
    double              *out_diff);

// Returns true if hash meets the network target derived from nbits (compact
// target). Hash must be in the same 32-byte big-endian format produced by
// mining_hash_from_state / sha256d — the same convention as meets_target.
// Uses nbits_to_target() to unpack the 256-bit target, then compares with
// meets_target(). nbits is the raw uint32 as stored in mining_work_t.nbits
// (matches the stratum job nbits field).
bool share_meets_network_target(const uint8_t hash[32], uint32_t nbits);

// Software re-verification of a candidate share produced by the HW-SHA path.
// Reconstructs the 80-byte block header from work + ver_bits + nonce, runs
// double-SHA256 in software, and compares against claimed[32].
// Returns true iff the SW recompute matches claimed exactly.
// Use this after share_validate() returns SHARE_VALID on the HW-SHA dongle
// path to catch classic-ESP32 DPORT partial-corruption where a corrupt HW
// hash passes the target compare but disagrees with a SW recompute.
bool share_reverify(const mining_work_t *work, uint32_t ver_bits,
                    uint32_t nonce, const uint8_t claimed[32]);
