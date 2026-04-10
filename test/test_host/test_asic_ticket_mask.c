#include "unity.h"
#include "asic_proto.h"
#include <math.h>

// Test: difficulty 1 -> mask_val=0 -> {0x00,0x00,0x00,0x00}
void test_asic_ticket_mask_diff_1(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0x00};
    asic_difficulty_to_mask(1.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 2 -> mask_val=1 -> {0x00,0x00,0x00,0x80}
void test_asic_ticket_mask_diff_2(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0x80};
    asic_difficulty_to_mask(2.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 4 -> mask_val=3 -> {0x00,0x00,0x00,0xC0}
void test_asic_ticket_mask_diff_4(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0xC0};
    asic_difficulty_to_mask(4.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 8 -> mask_val=7 -> {0x00,0x00,0x00,0xE0}
void test_asic_ticket_mask_diff_8(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0xE0};
    asic_difficulty_to_mask(8.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 16 -> mask_val=15=0x0F -> {0x00,0x00,0x00,0xF0}
void test_asic_ticket_mask_diff_16(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0xF0};
    asic_difficulty_to_mask(16.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 32 -> mask_val=31=0x1F -> {0x00,0x00,0x00,0xF8}
void test_asic_ticket_mask_diff_32(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0xF8};
    asic_difficulty_to_mask(32.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 64 -> mask_val=63=0x3F -> {0x00,0x00,0x00,0xFC}
void test_asic_ticket_mask_diff_64(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0xFC};
    asic_difficulty_to_mask(64.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 128 -> mask_val=127=0x7F -> {0x00,0x00,0x00,0xFE}
void test_asic_ticket_mask_diff_128(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0xFE};
    asic_difficulty_to_mask(128.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 256 -> mask_val=255=0xFF -> {0x00,0x00,0x00,0xFF}
void test_asic_ticket_mask_diff_256(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0xFF};
    asic_difficulty_to_mask(256.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 512 -> mask_val=0x1FF -> {0x00,0x00,0x80,0xFF}
void test_asic_ticket_mask_diff_512(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x80, 0xFF};
    asic_difficulty_to_mask(512.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 1024 -> mask_val=0x3FF -> {0x00,0x00,0xC0,0xFF}
void test_asic_ticket_mask_diff_1024(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0xC0, 0xFF};
    asic_difficulty_to_mask(1024.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 2048 -> mask_val=0x7FF -> {0x00,0x00,0xE0,0xFF}
void test_asic_ticket_mask_diff_2048(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0xE0, 0xFF};
    asic_difficulty_to_mask(2048.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty 4096 -> mask_val=0xFFF -> {0x00,0x00,0xF0,0xFF}
void test_asic_ticket_mask_diff_4096(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0xF0, 0xFF};
    asic_difficulty_to_mask(4096.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: non-power-of-2 (700) -> largest_power_of_2(700)=512 -> mask_val=511=0x1FF -> {0x00,0x00,0x80,0xFF}
void test_asic_ticket_mask_non_power_of_2_700(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x80, 0xFF};
    asic_difficulty_to_mask(700.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: non-power-of-2 (513) -> largest_power_of_2(513)=512 -> mask_val=511=0x1FF -> {0x00,0x00,0x80,0xFF}
void test_asic_ticket_mask_non_power_of_2_513(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x80, 0xFF};
    asic_difficulty_to_mask(513.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: non-power-of-2 (1000) -> largest_power_of_2(1000)=512 -> mask_val=511=0x1FF -> {0x00,0x00,0x80,0xFF}
void test_asic_ticket_mask_non_power_of_2_1000(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x80, 0xFF};
    asic_difficulty_to_mask(1000.0, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: difficulty below 1 -> clamped to 1 -> mask_val=0 -> {0x00,0x00,0x00,0x00}
void test_asic_ticket_mask_below_1(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0x00};
    asic_difficulty_to_mask(0.5, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: very small difficulty -> clamped to 1 -> mask_val=0 -> {0x00,0x00,0x00,0x00}
void test_asic_ticket_mask_very_small(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0x00};
    asic_difficulty_to_mask(0.001, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: boundary case (513.5) -> largest_power_of_2(513.5)=512 -> mask_val=511=0x1FF -> {0x00,0x00,0x80,0xFF}
void test_asic_ticket_mask_boundary_513_5(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x80, 0xFF};
    asic_difficulty_to_mask(513.5, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}

// Test: boundary case (512.1) -> largest_power_of_2(512.1)=512 -> mask_val=511=0x1FF -> {0x00,0x00,0x80,0xFF}
void test_asic_ticket_mask_boundary_512_1(void) {
    uint8_t mask[4];
    uint8_t expected[4] = {0x00, 0x00, 0x80, 0xFF};
    asic_difficulty_to_mask(512.1, mask);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mask, 4);
}
