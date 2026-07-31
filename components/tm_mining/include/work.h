#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Maximum sizes for stratum data
#define MAX_COINB1_SIZE     256
#define MAX_COINB2_SIZE     256
#define MAX_EXTRANONCE1_SIZE 8
#define MAX_EXTRANONCE2_SIZE 8
#define MAX_MERKLE_BRANCHES  16

// Stratum job data (parsed from mining.notify)
typedef struct {
    char     job_id[64];
    uint8_t  prevhash[32];       // raw prevhash (after endian fix)
    uint8_t  coinb1[MAX_COINB1_SIZE];
    size_t   coinb1_len;
    uint8_t  coinb2[MAX_COINB2_SIZE];
    size_t   coinb2_len;
    uint8_t  merkle_branches[MAX_MERKLE_BRANCHES][32];
    size_t   merkle_count;
    uint32_t version;
    uint32_t nbits;
    uint32_t ntime;
    bool     clean_jobs;
} stratum_job_t;

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
