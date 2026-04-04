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
    uint8_t  target[32];         // 256-bit target (big-endian for comparison)
    uint32_t version;            // for version rolling
    uint32_t version_mask;       // BIP 320: 0 if version rolling not active
    uint32_t ntime;              // for ntime rolling
    char     job_id[64];
    char     extranonce2_hex[17]; // extranonce2 as hex string (8 bytes = 16 hex chars + null)
    bool     clean;              // true = new block, interrupt mining immediately
} mining_work_t;

// Coinbase construction: coinb1 + extranonce1 + extranonce2 + coinb2 -> SHA256d
void build_coinbase_hash(const uint8_t *coinb1, size_t coinb1_len,
                         const uint8_t *extranonce1, size_t en1_len,
                         const uint8_t *extranonce2, size_t en2_len,
                         const uint8_t *coinb2, size_t coinb2_len,
                         uint8_t hash[32]);

// Merkle root: coinbase_hash + branches -> root
void build_merkle_root(const uint8_t coinbase_hash[32],
                       const uint8_t branches[][32], size_t branch_count,
                       uint8_t root[32]);

// Serialize 80-byte block header (all fields little-endian)
void serialize_header(uint32_t version, const uint8_t prevhash[32],
                      const uint8_t merkle_root[32], uint32_t ntime,
                      uint32_t nbits, uint32_t nonce,
                      uint8_t header[80]);

// Set nonce in an already-serialized header (bytes 76-79)
void set_header_nonce(uint8_t header[80], uint32_t nonce);

// Convert nbits (compact target) to 256-bit target (big-endian)
void nbits_to_target(uint32_t nbits, uint8_t target[32]);

// Check if hash meets target (both 32-byte values, compared as big-endian 256-bit integers)
// Returns true if hash <= target
bool meets_target(const uint8_t hash[32], const uint8_t target[32]);

// Convert pool difficulty to 256-bit target (little-endian, Bitcoin convention)
void difficulty_to_target(double diff, uint8_t target[32]);

// Convert stratum prevhash (8 groups of 4 bytes, each group reversed) to raw prevhash
void decode_stratum_prevhash(const char *hex, uint8_t prevhash[32]);

// Hex string to bytes utility. Returns number of bytes written.
size_t hex_to_bytes(const char *hex, uint8_t *out, size_t max_out);

// Bytes to hex string utility (null-terminated output expected to be at least 2*len+1)
void bytes_to_hex(const uint8_t *data, size_t len, char *hex);

// Iterate through BIP 320 version-rolling submask values.
// Start with ver_bits=0. Returns next ver_bits; 0 when exhausted.
static inline uint32_t next_version_roll(uint32_t ver_bits, uint32_t mask) {
    if (mask == 0) return 0;
    if (ver_bits == 0) return mask;
    uint32_t next = (ver_bits - 1) & mask;
    return next;  // 0 means exhausted
}
