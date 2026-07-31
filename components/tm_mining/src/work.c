#include "work.h"
#include "bb_byte_order.h"
#include <string.h>
#include <math.h>

void mining_hash_from_state(const uint32_t state[8], uint8_t hash_out[32]) {
    for (int i = 0; i < 8; i++) {
        bb_store_be32(hash_out + i * 4, state[i]);
    }
}

void set_header_nonce(uint8_t header[80], uint32_t nonce) {
    bb_store_le32(header + 76, nonce);
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

bool is_target_valid(const uint8_t target[32])
{
    // Reject all-zero target (every hash passes meets_target)
    bool all_zero = true;
    for (int i = 0; i < 32; i++) {
        if (target[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) return false;

    // Reject all-0xFF target (trivially easy)
    bool all_ff = true;
    for (int i = 0; i < 32; i++) {
        if (target[i] != 0xFF) { all_ff = false; break; }
    }
    if (all_ff) return false;

    // LE MSB bytes must be zero (Bitcoin PoW structure)
    // target[31] and target[30] == 0 ≈ pool diff >= 0.004
    if (target[31] != 0 || target[30] != 0) return false;

    return true;
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
