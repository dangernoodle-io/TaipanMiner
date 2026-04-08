#include "work.h"
#include "sha256.h"
#include <string.h>
#include <ctype.h>
#include <math.h>

// Utility to write uint32 in little-endian format
static void write_le32(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

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
    write_le32(header, version);

    // Bytes 4-35: prevhash (32 bytes, already in correct byte order)
    memcpy(header + 4, prevhash, 32);

    // Bytes 36-67: merkle_root (32 bytes)
    memcpy(header + 36, merkle_root, 32);

    // Bytes 68-71: ntime (uint32 LE)
    write_le32(header + 68, ntime);

    // Bytes 72-75: nbits (uint32 LE)
    write_le32(header + 72, nbits);

    // Bytes 76-79: nonce (uint32 LE)
    write_le32(header + 76, nonce);
}

void set_header_nonce(uint8_t header[80], uint32_t nonce) {
    write_le32(header + 76, nonce);
}

void nbits_to_target(uint32_t nbits, uint8_t target[32]) {
    memset(target, 0, 32);

    uint32_t exponent = (nbits >> 24) & 0xFF;
    uint32_t coefficient = nbits & 0x7FFFFF;
    if (nbits & 0x800000) {
        return;  // negative coefficient, invalid for mining
    }

    if (exponent == 0 || coefficient == 0) {
        return;
    }

    // Target = coefficient * 256^(exponent-3)
    // In big-endian 32-byte array: coefficient MSB at byte (32 - exponent)
    int pos = 32 - (int)exponent;

    // Place 3 coefficient bytes in big-endian order
    uint8_t c[3] = {
        (uint8_t)((coefficient >> 16) & 0xFF),
        (uint8_t)((coefficient >> 8) & 0xFF),
        (uint8_t)(coefficient & 0xFF),
    };

    for (int i = 0; i < 3; i++) {
        int idx = pos + i;
        if (idx >= 0 && idx < 32) {
            target[idx] = c[i];
        }
    }
}

bool meets_target(const uint8_t hash[32], const uint8_t target[32]) {
    // Compare hash vs target as little-endian 256-bit integers
    // byte[31] is MSB, byte[0] is LSB (Bitcoin convention)
    // Return true if hash <= target

    for (int i = 31; i >= 0; i--) {
        if (hash[i] < target[i]) {
            return true;
        }
        if (hash[i] > target[i]) {
            return false;
        }
    }

    return true;
}

void decode_stratum_prevhash(const char *hex, uint8_t prevhash[32]) {
    // Stratum sends prevhash as 64 hex chars = 32 bytes
    // It's organized as 8 groups of 4 bytes (8 hex chars each)
    // Each group has its bytes reversed

    // First, convert hex to raw bytes
    uint8_t raw[32];
    hex_to_bytes(hex, raw, 32);

    // Now reverse bytes within each 4-byte group
    for (int i = 0; i < 8; i++) {
        int group_start = i * 4;
        prevhash[group_start] = raw[group_start + 3];
        prevhash[group_start + 1] = raw[group_start + 2];
        prevhash[group_start + 2] = raw[group_start + 1];
        prevhash[group_start + 3] = raw[group_start];
    }
}

size_t hex_to_bytes(const char *hex, uint8_t *out, size_t max_out) {
    if (!hex || !out) {
        return 0;
    }

    size_t count = 0;
    size_t hex_len = strlen(hex);

    // Process pairs of hex characters
    for (size_t i = 0; i + 1 < hex_len && count < max_out; i += 2) {
        char high = hex[i];
        char low = hex[i + 1];

        // Convert hex chars to nibbles
        uint8_t high_nibble = 0;
        uint8_t low_nibble = 0;

        if (high >= '0' && high <= '9') {
            high_nibble = high - '0';
        } else if (high >= 'a' && high <= 'f') {
            high_nibble = 10 + (high - 'a');
        } else if (high >= 'A' && high <= 'F') {
            high_nibble = 10 + (high - 'A');
        }

        if (low >= '0' && low <= '9') {
            low_nibble = low - '0';
        } else if (low >= 'a' && low <= 'f') {
            low_nibble = 10 + (low - 'a');
        } else if (low >= 'A' && low <= 'F') {
            low_nibble = 10 + (low - 'A');
        }

        out[count] = (high_nibble << 4) | low_nibble;
        count++;
    }

    return count;
}

void bytes_to_hex(const uint8_t *data, size_t len, char *hex) {
    if (!data || !hex) {
        return;
    }

    const char hex_chars[] = "0123456789abcdef";

    for (size_t i = 0; i < len; i++) {
        hex[2 * i] = hex_chars[(data[i] >> 4) & 0xF];
        hex[2 * i + 1] = hex_chars[data[i] & 0xF];
    }

    hex[2 * len] = '\0';
}

void difficulty_to_target(double diff, uint8_t target[32])
{
    memset(target, 0, 32);
    if (diff <= 0) {
        memset(target, 0xff, 32);
        return;
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

double hash_to_difficulty(const uint8_t hash[32])
{
    // Hash is little-endian: byte[31] = MSB, byte[0] = LSB
    // Count leading zero bytes from MSB end
    int zero_bytes = 0;
    while (zero_bytes < 28 && hash[31 - zero_bytes] == 0) {
        zero_bytes++;
    }

    // Read 4 significant bytes (big-endian order) from MSB position
    int pos = 31 - zero_bytes;
    uint32_t sig = ((uint32_t)hash[pos] << 24) |
                   ((uint32_t)hash[pos - 1] << 16) |
                   ((uint32_t)hash[pos - 2] << 8) |
                   hash[pos - 3];

    if (sig == 0) return 1e15;

    // Bitcoin difficulty 1 target: 0x00000000 FFFF0000 00...00 (big-endian)
    // In little-endian byte order: 00...00 0000FFFF 00000000
    // Significant bytes at offset 28 from LSB with value 0xFFFF0000
    double base_diff = (double)0xFFFF0000UL / (double)sig;
    int shift_bits = (zero_bytes - 4) * 8;
    return ldexp(base_diff, shift_bits);
}
