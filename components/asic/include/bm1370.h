#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// --- Packet types ---
#define BM1370_TYPE_JOB       0x20
#define BM1370_TYPE_CMD       0x40

// --- Address groups ---
#define BM1370_GROUP_SINGLE   0x00
#define BM1370_GROUP_ALL      0x10

// --- Commands ---
#define BM1370_CMD_SETADDR    0x00
#define BM1370_CMD_WRITE      0x01
#define BM1370_CMD_READ       0x02
#define BM1370_CMD_INACTIVE   0x03

// --- Chip ---
#define BM1370_CHIP_ID        0x1370
#define BM1370_CORES          80
#define BM1370_SMALL_CORES    16
#define BM1370_JOB_ID_STEP    24
#define BM1370_JOB_ID_MOD     128

// --- Preambles ---
#define BM1370_PREAMBLE_TX_0  0x55
#define BM1370_PREAMBLE_TX_1  0xAA
#define BM1370_PREAMBLE_RX_0  0xAA
#define BM1370_PREAMBLE_RX_1  0x55

// --- Register addresses ---
#define BM1370_REG_CHIP_ID    0x00
#define BM1370_REG_PLL        0x08
#define BM1370_REG_HASH_COUNT 0x10
#define BM1370_REG_MISC_CTRL  0x18
#define BM1370_REG_FAST_UART  0x28
#define BM1370_REG_CORE_CTRL  0x3C
#define BM1370_REG_ANALOG_MUX 0x54
#define BM1370_REG_IO_DRV     0x58
#define BM1370_REG_VERSION    0xA4
#define BM1370_REG_A8         0xA8
#define BM1370_REG_MISC_SET   0xB9
#define BM1370_REG_TICKET_MASK  0x14

// --- Version mask (ASICBoost BIP 320) ---
#define BM1370_VERSION_MASK   0x1FFFE000

// --- Packet sizes ---
#define BM1370_JOB_DATA_LEN   82
#define BM1370_JOB_PKT_LEN    88   // 2 preamble + 1 header + 1 length + 82 data + 2 CRC16
#define BM1370_NONCE_LEN      11

// --- Job struct (82 bytes, wire format) ---
typedef struct __attribute__((__packed__)) {
    uint8_t  job_id;
    uint8_t  num_midstates;
    uint8_t  starting_nonce[4];
    uint8_t  nbits[4];
    uint8_t  ntime[4];
    uint8_t  merkle_root[32];
    uint8_t  prev_block_hash[32];
    uint8_t  version[4];
} bm1370_job_t;

// --- Nonce response (11 bytes, wire format) ---
typedef struct __attribute__((__packed__)) {
    uint8_t  preamble[2];       // 0xAA 0x55
    uint8_t  nonce[4];          // big-endian
    uint8_t  midstate_num;
    uint8_t  job_id;            // bits[7:4] = (real_job_id >> 1), bits[3:0] = small_core_id
    uint8_t  version_bits[2];   // big-endian; (ntohs(v) << 13) gives rolled version bits
    uint8_t  crc_flags;         // bits[4:0] = CRC5, bit[7] = is_job_response
} bm1370_nonce_t;

// --- Framing functions (implemented in bm1370.c) ---

// Build a command packet. Returns packet size, or 0 on error.
size_t bm1370_build_cmd(uint8_t *buf, size_t buflen, uint8_t cmd, uint8_t group,
                        const uint8_t *data, uint8_t data_len);

// Build an 88-byte job packet. Returns BM1370_JOB_PKT_LEN on success, 0 on error.
size_t bm1370_build_job(uint8_t *buf, size_t buflen, const bm1370_job_t *job);

// Parse an 11-byte nonce response. Returns true if preamble valid.
bool bm1370_parse_nonce(const uint8_t *buf, size_t len, bm1370_nonce_t *out);

// Extract BM1370 job fields from a serialized 80-byte block header.
void bm1370_extract_job(const uint8_t header[80], uint8_t job_id, bm1370_job_t *job);

// Decode the real job ID from a nonce response: (job_id_byte & 0xF0) >> 1
uint8_t bm1370_decode_job_id(const bm1370_nonce_t *nonce);

// Decode rolled version bits from a nonce response: ntohs(version_bits) << 13
uint32_t bm1370_decode_version_bits(const bm1370_nonce_t *nonce);

// Compute the 4-byte BM1370 ticket mask for a given pool difficulty.
// Output is in wire format (BE, per-byte bit-reversed) ready to write to register 0x14.
void bm1370_difficulty_to_mask(double difficulty, uint8_t mask_out[4]);
