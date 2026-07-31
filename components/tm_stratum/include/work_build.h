#pragma once

#include <stdint.h>
#include <stddef.h>

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

// Convert pool difficulty to 256-bit target (little-endian, Bitcoin convention)
void difficulty_to_target(double diff, uint8_t target[32]);

// Convert stratum prevhash (8 groups of 4 bytes, each group reversed) to raw prevhash
void decode_stratum_prevhash(const char *hex, uint8_t prevhash[32]);
