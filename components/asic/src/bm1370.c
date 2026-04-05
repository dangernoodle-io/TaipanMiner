#include "bm1370.h"
#include "crc.h"
#include <string.h>
#include <math.h>

size_t bm1370_build_cmd(uint8_t *buf, size_t buflen, uint8_t cmd, uint8_t group,
                        const uint8_t *data, uint8_t data_len) {
    // Check buffer space: 5 bytes header/footer + data_len
    size_t pkt_len = 5 + data_len;
    if (buflen < pkt_len) {
        return 0;
    }

    // buf[0] = 0x55 (BM1370_PREAMBLE_TX_0)
    // buf[1] = 0xAA (BM1370_PREAMBLE_TX_1)
    buf[0] = BM1370_PREAMBLE_TX_0;
    buf[1] = BM1370_PREAMBLE_TX_1;

    // buf[2] = header byte: BM1370_TYPE_CMD | group | cmd
    buf[2] = BM1370_TYPE_CMD | group | cmd;

    // buf[3] = length byte: data_len + 3 (data + header + length + crc5)
    buf[3] = data_len + 3;

    // buf[4..4+data_len-1] = data bytes
    if (data_len > 0) {
        memcpy(buf + 4, data, data_len);
    }

    // buf[4+data_len] = crc5(buf+2, data_len+2)
    // CRC5 covers: header(1) + length(1) + data(data_len)
    buf[4 + data_len] = crc5(buf + 2, 2 + data_len);

    return pkt_len;
}

size_t bm1370_build_job(uint8_t *buf, size_t buflen, const bm1370_job_t *job) {
    // Check buffer space: need 88 bytes
    if (buflen < BM1370_JOB_PKT_LEN) {
        return 0;
    }

    // buf[0] = 0x55 (preamble)
    // buf[1] = 0xAA (preamble)
    buf[0] = BM1370_PREAMBLE_TX_0;
    buf[1] = BM1370_PREAMBLE_TX_1;

    // buf[2] = header byte: BM1370_TYPE_JOB | BM1370_GROUP_SINGLE | BM1370_CMD_WRITE
    buf[2] = BM1370_TYPE_JOB | BM1370_GROUP_SINGLE | BM1370_CMD_WRITE;

    // buf[3] = length byte: BM1370_JOB_DATA_LEN + 4 = 82 + 4 = 86
    // (length = job_data(82) + header(1) + length(1) + crc16(2))
    buf[3] = BM1370_JOB_DATA_LEN + 4;

    // buf[4..85] = memcpy of the 82-byte job struct
    memcpy(buf + 4, (const uint8_t *)job, BM1370_JOB_DATA_LEN);

    // Compute CRC16 over header(1) + length(1) + job(82) = 84 bytes
    uint16_t crc = crc16_false(buf + 2, BM1370_JOB_DATA_LEN + 2);

    // buf[86] = CRC16 high byte
    // buf[87] = CRC16 low byte
    buf[86] = (uint8_t)(crc >> 8);
    buf[87] = (uint8_t)(crc & 0xFF);

    return BM1370_JOB_PKT_LEN;
}

bool bm1370_parse_nonce(const uint8_t *buf, size_t len, bm1370_nonce_t *out) {
    // Check minimum length
    if (len < BM1370_NONCE_LEN) {
        return false;
    }

    // Check preamble: buf[0] = 0xAA, buf[1] = 0x55
    if (buf[0] != BM1370_PREAMBLE_RX_0 || buf[1] != BM1370_PREAMBLE_RX_1) {
        return false;
    }

    // Copy entire nonce packet (11 bytes)
    memcpy((uint8_t *)out, buf, BM1370_NONCE_LEN);

    return true;
}

// Reverse a byte array in place
static void reverse_bytes(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len / 2; i++) {
        uint8_t tmp = buf[i];
        buf[i] = buf[len - 1 - i];
        buf[len - 1 - i] = tmp;
    }
}

// Swap bytes within each 4-byte word
static void swap_endian_words(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i + 3 < len; i += 4) {
        uint8_t t0 = buf[i], t1 = buf[i + 1];
        buf[i] = buf[i + 3];
        buf[i + 1] = buf[i + 2];
        buf[i + 2] = t1;
        buf[i + 3] = t0;
    }
}

void bm1370_extract_job(const uint8_t header[80], uint8_t job_id, bm1370_job_t *job) {
    // Header layout (standard Bitcoin, all little-endian):
    // bytes 0-3: version
    // bytes 4-35: prevhash (32 bytes)
    // bytes 36-67: merkle_root (32 bytes)
    // bytes 68-71: ntime (uint32 LE)
    // bytes 72-75: nbits (uint32 LE)
    // bytes 76-79: nonce (uint32 LE)

    job->job_id = job_id;
    job->num_midstates = 1;

    // starting_nonce = 0 (ASIC will scan nonces itself)
    memset(job->starting_nonce, 0, 4);

    // nbits, ntime, version — raw copy (LE, same as header)
    memcpy(job->nbits, header + 72, 4);
    memcpy(job->ntime, header + 68, 4);
    memcpy(job->version, header + 0, 4);

    // merkle_root — swap endian words then reverse entire 32 bytes
    memcpy(job->merkle_root, header + 36, 32);
    swap_endian_words(job->merkle_root, 32);
    reverse_bytes(job->merkle_root, 32);

    // prev_block_hash — swap endian words then reverse entire 32 bytes
    // (header has bswap32 per word from decode_stratum_prevhash; ASIC needs word-order reversed)
    memcpy(job->prev_block_hash, header + 4, 32);
    swap_endian_words(job->prev_block_hash, 32);
    reverse_bytes(job->prev_block_hash, 32);
}

uint8_t bm1370_decode_job_id(const bm1370_nonce_t *nonce) {
    return (nonce->job_id & 0xF0) >> 1;
}

uint32_t bm1370_decode_version_bits(const bm1370_nonce_t *nonce) {
    // Manual big-endian to host conversion of 2-byte version_bits field
    uint16_t v = ((uint16_t)nonce->version_bits[0] << 8) | nonce->version_bits[1];
    return (uint32_t)v << 13;
}

// Reverse bits in a single byte
static uint8_t reverse_bits(uint8_t b)
{
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
    return b;
}

// Largest power of 2 <= n
static uint32_t largest_power_of_two(uint32_t n)
{
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return (n + 1) >> 1;
}

void bm1370_difficulty_to_mask(double difficulty, uint8_t mask_out[4])
{
    if (difficulty < 1.0) difficulty = 1.0;
    uint32_t diff = (uint32_t)difficulty;
    if (diff < 1) diff = 1;

    // BM1370 ticket mask = (largest_power_of_2(diff) - 1), BE, per-byte bit-reversed
    uint32_t mask_val = largest_power_of_two(diff) - 1;

    // BE decomposition then per-byte bit-reversal
    mask_out[0] = reverse_bits((uint8_t)(mask_val >> 24));
    mask_out[1] = reverse_bits((uint8_t)(mask_val >> 16));
    mask_out[2] = reverse_bits((uint8_t)(mask_val >> 8));
    mask_out[3] = reverse_bits((uint8_t)(mask_val));
}
