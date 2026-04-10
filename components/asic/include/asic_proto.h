#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// --- Fixed ticket mask difficulty ---
// ASIC produces zero nonces above ~512.
// Local SHA256d checks each nonce against pool target before submission.
#define ASIC_TICKET_DIFF 256.0

// --- Packet types ---
#define ASIC_TYPE_JOB       0x20
#define ASIC_TYPE_CMD       0x40

// --- Address groups ---
#define ASIC_GROUP_SINGLE   0x00
#define ASIC_GROUP_ALL      0x10

// --- Commands ---
#define ASIC_CMD_SETADDR    0x00
#define ASIC_CMD_WRITE      0x01
#define ASIC_CMD_READ       0x02
#define ASIC_CMD_INACTIVE   0x03

// --- Ticket mask register (chip-specific; define in chip header) ---
// Placeholder — will be overridden by chip-specific header
#ifndef ASIC_REG_TICKET_MASK
#define ASIC_REG_TICKET_MASK 0x14
#endif

// --- Job ID parameters ---
#define ASIC_JOB_ID_STEP    24
#define ASIC_JOB_ID_MOD     128

// --- Preambles ---
#define ASIC_PREAMBLE_TX_0  0x55
#define ASIC_PREAMBLE_TX_1  0xAA
#define ASIC_PREAMBLE_RX_0  0xAA
#define ASIC_PREAMBLE_RX_1  0x55

// --- Version mask (ASICBoost BIP 320) ---
#define ASIC_VERSION_MASK   0x1FFFE000

// --- Packet sizes ---
#define ASIC_JOB_DATA_LEN   82
#define ASIC_JOB_PKT_LEN    88   // 2 preamble + 1 header + 1 length + 82 data + 2 CRC16
#define ASIC_NONCE_LEN      11

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
} asic_job_t;

// --- Nonce response (11 bytes, wire format) ---
typedef struct __attribute__((__packed__)) {
    uint8_t  preamble[2];       // 0xAA 0x55
    uint8_t  nonce[4];          // big-endian
    uint8_t  midstate_num;
    uint8_t  job_id;            // bits[7:4] = (real_job_id >> 1), bits[3:0] = small_core_id
    uint8_t  version_bits[2];   // big-endian; (ntohs(v) << 13) gives rolled version bits
    uint8_t  crc_flags;         // bits[4:0] = CRC5, bit[7] = is_job_response
} asic_nonce_t;

// --- Framing functions (implemented in asic_proto.c) ---

// Build a command packet. Returns packet size, or 0 on error.
size_t asic_build_cmd(uint8_t *buf, size_t buflen, uint8_t cmd, uint8_t group,
                      const uint8_t *data, uint8_t data_len);

// Build an 88-byte job packet. Returns ASIC_JOB_PKT_LEN on success, 0 on error.
size_t asic_build_job(uint8_t *buf, size_t buflen, const asic_job_t *job);

// Parse an 11-byte nonce response. Returns true if preamble valid.
bool asic_parse_nonce(const uint8_t *buf, size_t len, asic_nonce_t *out);

// Extract ASIC job fields from a serialized 80-byte block header.
void asic_extract_job(const uint8_t header[80], uint8_t job_id, asic_job_t *job);

// Decode the real job ID from a nonce response: (job_id_byte & 0xF0) >> 1
uint8_t asic_decode_job_id(const asic_nonce_t *nonce);

// Decode rolled version bits from a nonce response: ntohs(version_bits) << 13
uint32_t asic_decode_version_bits(const asic_nonce_t *nonce);

// Compute the 4-byte ASIC ticket mask for a given pool difficulty.
// Output is in wire format (BE, per-byte bit-reversed) ready to write to register 0x14.
void asic_difficulty_to_mask(double difficulty, uint8_t mask_out[4]);
