#include "unity.h"
#include "bm1370.h"
#include "crc.h"
#include <string.h>

void test_build_cmd_inactive(void) {
    // Chain inactive command should produce: 55 AA 53 05 00 00 03
    uint8_t buf[16];
    uint8_t data[] = {0x00, 0x00};
    size_t len = bm1370_build_cmd(buf, sizeof(buf), BM1370_CMD_INACTIVE, BM1370_GROUP_ALL, data, 2);
    TEST_ASSERT_EQUAL(7, len);
    uint8_t expected[] = {0x55, 0xAA, 0x53, 0x05, 0x00, 0x00, 0x03};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, 7);
}

void test_build_cmd_read_chipid(void) {
    // Read chip ID from all chips: 55 AA 52 05 00 00 [CRC5]
    // Header = TYPE_CMD(0x40) | GROUP_ALL(0x10) | CMD_READ(0x02) = 0x52
    uint8_t buf[16];
    uint8_t data[] = {0x00, 0x00};
    size_t len = bm1370_build_cmd(buf, sizeof(buf), BM1370_CMD_READ, BM1370_GROUP_ALL, data, 2);
    TEST_ASSERT_EQUAL(7, len);
    TEST_ASSERT_EQUAL_HEX8(0x55, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x52, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[3]);
    // CRC5 over {0x52, 0x05, 0x00, 0x00}
    uint8_t crc_data[] = {0x52, 0x05, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8(crc5(crc_data, 4), buf[6]);
}

void test_build_cmd_buffer_too_small(void) {
    uint8_t buf[4];  // too small for 7-byte packet
    uint8_t data[] = {0x00, 0x00};
    size_t len = bm1370_build_cmd(buf, sizeof(buf), BM1370_CMD_INACTIVE, BM1370_GROUP_ALL, data, 2);
    TEST_ASSERT_EQUAL(0, len);
}

void test_build_job_size(void) {
    uint8_t buf[128];
    bm1370_job_t job;
    memset(&job, 0, sizeof(job));
    job.job_id = 0x42;
    job.num_midstates = 1;
    size_t len = bm1370_build_job(buf, sizeof(buf), &job);
    TEST_ASSERT_EQUAL(BM1370_JOB_PKT_LEN, len);  // 88
    TEST_ASSERT_EQUAL_HEX8(0x55, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x21, buf[2]);  // TYPE_JOB | GROUP_SINGLE | CMD_WRITE
    TEST_ASSERT_EQUAL_HEX8(0x56, buf[3]);  // 86 = 82 + 4
}

void test_build_job_crc16(void) {
    // Build a job and verify CRC16 in last 2 bytes
    uint8_t buf[128];
    bm1370_job_t job;
    memset(&job, 0xAB, sizeof(job));
    job.job_id = 0x10;
    job.num_midstates = 1;
    size_t len = bm1370_build_job(buf, sizeof(buf), &job);
    TEST_ASSERT_EQUAL(88, len);
    // Recompute CRC16 over buf[2..85] = 84 bytes
    uint16_t expected_crc = crc16_false(buf + 2, 84);
    uint16_t actual_crc = ((uint16_t)buf[86] << 8) | buf[87];
    TEST_ASSERT_EQUAL_HEX16(expected_crc, actual_crc);
}

void test_extract_job_from_header(void) {
    // Bitcoin genesis block header (80 bytes, all little-endian)
    uint8_t header[80];
    memset(header, 0, 80);
    // version = 1 (LE: 01 00 00 00)
    header[0] = 0x01;
    // prevhash = all zeros (genesis)
    // merkle_root: first 4 bytes = 0x3B 0xA3 0xED 0xFD (genesis merkle, LE)
    header[36] = 0x3B; header[37] = 0xA3; header[38] = 0xED; header[39] = 0xFD;
    // ntime = 0x495FAB29 (LE: 29 AB 5F 49)
    header[68] = 0x29; header[69] = 0xAB; header[70] = 0x5F; header[71] = 0x49;
    // nbits = 0x1D00FFFF (LE: FF FF 00 1D)
    header[72] = 0xFF; header[73] = 0xFF; header[74] = 0x00; header[75] = 0x1D;

    bm1370_job_t job;
    bm1370_extract_job(header, 42, &job);

    TEST_ASSERT_EQUAL(42, job.job_id);
    TEST_ASSERT_EQUAL(1, job.num_midstates);
    // starting_nonce should be all zeros
    uint8_t zero4[4] = {0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero4, job.starting_nonce, 4);
    // version copied raw from header[0..3]
    TEST_ASSERT_EQUAL_HEX8(0x01, job.version[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, job.version[1]);
    // nbits copied raw from header[72..75]
    TEST_ASSERT_EQUAL_HEX8(0xFF, job.nbits[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, job.nbits[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, job.nbits[2]);
    TEST_ASSERT_EQUAL_HEX8(0x1D, job.nbits[3]);
    // ntime copied raw from header[68..71]
    TEST_ASSERT_EQUAL_HEX8(0x29, job.ntime[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, job.ntime[1]);
    // merkle_root transformed: swap_endian_words + reverse for ASIC format
    // original bytes [0x3B, 0xA3, 0xED, 0xFD] at word 0 → reversed to end
    TEST_ASSERT_EQUAL_HEX8(0x3B, job.merkle_root[28]);
    TEST_ASSERT_EQUAL_HEX8(0xA3, job.merkle_root[29]);
    TEST_ASSERT_EQUAL_HEX8(0xED, job.merkle_root[30]);
    TEST_ASSERT_EQUAL_HEX8(0xFD, job.merkle_root[31]);
}

void test_parse_nonce_valid(void) {
    // Construct a valid 11-byte nonce response
    uint8_t buf[11] = {
        0xAA, 0x55,           // preamble
        0x12, 0x34, 0x56, 0x78, // nonce (big-endian)
        0x00,                 // midstate_num
        0xA4,                 // job_id byte (upper nibble 0xA0 -> real id = (0xA0>>1) = 0x50)
        0x00, 0x03,           // version_bits (big-endian)
        0x87                  // crc_flags (bit7=1 job response, bits[4:0] = CRC5)
    };
    bm1370_nonce_t nonce;
    TEST_ASSERT_TRUE(bm1370_parse_nonce(buf, 11, &nonce));
    TEST_ASSERT_EQUAL_HEX8(0xAA, nonce.preamble[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, nonce.preamble[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, nonce.nonce[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA4, nonce.job_id);
}

void test_parse_nonce_bad_preamble(void) {
    uint8_t buf[11] = {0x55, 0xAA, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // wrong preamble order
    bm1370_nonce_t nonce;
    TEST_ASSERT_FALSE(bm1370_parse_nonce(buf, 11, &nonce));
}

void test_parse_nonce_too_short(void) {
    uint8_t buf[5] = {0xAA, 0x55, 0, 0, 0};
    bm1370_nonce_t nonce;
    TEST_ASSERT_FALSE(bm1370_parse_nonce(buf, 5, &nonce));
}

void test_decode_job_id(void) {
    // job_id byte = 0xA4: upper nibble = 0xA0, (0xA0 >> 1) = 0x50
    bm1370_nonce_t nonce;
    memset(&nonce, 0, sizeof(nonce));
    nonce.job_id = 0xA4;
    TEST_ASSERT_EQUAL(0x50, bm1370_decode_job_id(&nonce));
}

void test_decode_version_bits(void) {
    // version_bits = {0x00, 0x03} -> ntohs = 0x0003, << 13 = 0x00006000
    bm1370_nonce_t nonce;
    memset(&nonce, 0, sizeof(nonce));
    nonce.version_bits[0] = 0x00;
    nonce.version_bits[1] = 0x03;
    TEST_ASSERT_EQUAL_HEX32(0x00006000, bm1370_decode_version_bits(&nonce));
}

void test_decode_version_bits_max(void) {
    // version_bits = {0xFF, 0xFF} -> 0xFFFF << 13 = 0x1FFFE000
    bm1370_nonce_t nonce;
    memset(&nonce, 0, sizeof(nonce));
    nonce.version_bits[0] = 0xFF;
    nonce.version_bits[1] = 0xFF;
    TEST_ASSERT_EQUAL_HEX32(0x1FFFE000, bm1370_decode_version_bits(&nonce));
}
