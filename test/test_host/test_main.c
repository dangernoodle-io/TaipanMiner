#include "unity.h"

// Forward declarations from test_sha256.c
void test_sha256_empty_string(void);
void test_sha256_abc(void);
void test_sha256_two_blocks(void);
void test_sha256d_known(void);
void test_sha256_midstate(void);
void test_sha256_genesis_header(void);
void test_sha256_transform_words(void);
void test_sha256_transform_performance(void);

// Forward declarations from test_work.c
void test_hex_to_bytes(void);
void test_bytes_to_hex(void);
void test_hex_roundtrip(void);
void test_serialize_header_genesis(void);
void test_set_header_nonce(void);
void test_nbits_to_target_genesis(void);
void test_nbits_to_target_high_diff(void);
void test_meets_target_pass(void);
void test_meets_target_fail(void);
void test_meets_target_equal(void);
void test_build_coinbase_hash(void);
void test_build_merkle_root_no_branches(void);
void test_build_merkle_root_with_branches(void);
void test_decode_stratum_prevhash(void);
void test_block1_full_pipeline(void);
void test_block170_merkle_and_hash(void);
void test_decode_stratum_prevhash_real(void);
void test_stratum_pipeline_block1(void);
void test_difficulty_to_target_diff1(void);
void test_difficulty_to_target_easy(void);
void test_difficulty_to_target_hard(void);
void test_mining_round_trip_block1(void);
void test_mining_early_reject_byte_order(void);
void test_difficulty_target_meets_target_integration(void);
void test_version_rolling_mask_increment(void);

// Target validation tests (test_work.c)
void test_difficulty_to_target_nan(void);
void test_difficulty_to_target_inf(void);
void test_difficulty_to_target_neg_inf(void);
void test_difficulty_to_target_negative(void);
void test_difficulty_to_target_zero(void);
void test_difficulty_to_target_tiny(void);
void test_difficulty_to_target_normal(void);
void test_is_target_valid_all_zero(void);
void test_is_target_valid_all_ff(void);
void test_is_target_valid_nonzero_msb31(void);
void test_is_target_valid_nonzero_msb30(void);
void test_is_target_valid_diff1(void);
void test_is_target_valid_diff512(void);
void test_is_target_valid_diff_001(void);

// Forward declarations from test_nv_config.c
void test_nv_config_init(void);
void test_nv_config_all_empty_before_provisioning(void);
void test_nv_config_not_provisioned_by_default(void);

// Forward declarations from test_crc.c
void test_crc5_chain_inactive(void);
void test_crc5_reg_write_a8(void);
void test_crc5_reg_write_3c(void);
void test_crc5_set_address(void);
void test_crc16_false_standard(void);
void test_crc16_false_empty(void);

// Forward declarations from test_pll.c
void test_pll_500mhz(void);
void test_pll_600mhz(void);
void test_pll_400mhz(void);
void test_pll_vdo_scale_high(void);
void test_pll_vdo_scale_low(void);
void test_pll_postdiv_byte(void);

// Forward declarations from test_asic_proto.c
void test_build_cmd_inactive(void);
void test_build_cmd_read_chipid(void);
void test_build_cmd_buffer_too_small(void);
void test_build_job_size(void);
void test_build_job_crc16(void);
void test_extract_job_from_header(void);
void test_parse_nonce_valid(void);
void test_parse_nonce_bad_preamble(void);
void test_parse_nonce_too_short(void);
void test_decode_job_id(void);
void test_decode_version_bits(void);
void test_decode_version_bits_max(void);

// Forward declarations from test_asic_ticket_mask.c
void test_asic_ticket_mask_diff_1(void);
void test_asic_ticket_mask_diff_2(void);
void test_asic_ticket_mask_diff_4(void);
void test_asic_ticket_mask_diff_8(void);
void test_asic_ticket_mask_diff_256(void);
void test_asic_ticket_mask_diff_512(void);
void test_asic_ticket_mask_diff_1024(void);
void test_asic_ticket_mask_diff_2048(void);
void test_asic_ticket_mask_diff_4096(void);
void test_asic_ticket_mask_non_power_of_2_700(void);
void test_asic_ticket_mask_non_power_of_2_513(void);
void test_asic_ticket_mask_non_power_of_2_1000(void);
void test_asic_ticket_mask_below_1(void);
void test_asic_ticket_mask_very_small(void);
void test_asic_ticket_mask_diff_16(void);
void test_asic_ticket_mask_diff_32(void);
void test_asic_ticket_mask_diff_64(void);
void test_asic_ticket_mask_diff_128(void);
void test_asic_ticket_mask_boundary_513_5(void);
void test_asic_ticket_mask_boundary_512_1(void);

// Forward declarations from test_mining.c
void test_sw_backend_finds_block1_share(void);
void test_sw_backend_early_reject_low_diff(void);
void test_sw_backend_early_reject_high_diff(void);
void test_mine_nonce_range_counts(void);
void test_mine_nonce_range_stops_on_hit(void);
void test_mine_result_has_version_hex(void);
void test_mine_nonce_range_no_hit(void);
void test_pack_target_word0_diff1(void);
void test_pack_target_word0_easy_diff(void);
void test_pack_target_word0_hard_diff(void);
void test_build_block2_padding(void);
void test_package_result_no_version_rolling(void);
void test_package_result_version_rolling_submits_ver_bits(void);

// Forward declarations from test_http_utils.c
void test_url_decode_basic(void);
void test_url_decode_plus_as_space(void);
void test_url_decode_hex_decode(void);
void test_url_decode_missing_field(void);
void test_url_decode_truncation(void);
void test_url_decode_percent_at_end(void);
void test_url_decode_field_not_first(void);
void test_url_decode_empty_value(void);
void test_url_decode_field_at_end(void);

// Forward declarations from test_stratum.c
void test_format_submit_no_version(void);
void test_format_submit_with_version(void);
void test_format_submit_null_version(void);
void test_format_submit_truncation(void);
void test_format_request_basic(void);
void test_format_request_truncation(void);
void test_format_request_newline(void);

// Forward declarations from test_stats.c
void test_ema_seeds_on_first_sample(void);
void test_ema_converges(void);
void test_ema_decay(void);
void test_hash_to_difficulty_leading_zeros(void);
void test_hash_to_difficulty_diff1(void);
void test_hash_to_difficulty_easy(void);
void test_hash_to_difficulty_six_zeros(void);
void test_best_diff_only_increases(void);

// Forward declarations from test_bm1368.c
void test_bm1368_pll_fb_range(void);
void test_bm1368_pll_490mhz(void);
void test_bm1368_pll_vdo_scale(void);
void test_asic_ticket_mask_256_bm1368(void);

// Forward declarations from test_tps546_decode.c
void test_ulinear16_typical_vout(void);
void test_ulinear16_zero(void);
void test_slinear11_positive_exp_neg(void);
void test_slinear11_negative_mantissa(void);
void test_slinear11_zero_mantissa(void);

// Forward declarations from test_emc2101_curve.c
void test_curve_below_lower_bound(void);
void test_curve_at_lower_bound(void);
void test_curve_mid_segment_one(void);
void test_curve_at_segment_boundary(void);
void test_curve_mid_segment_two(void);
void test_curve_at_upper_bound(void);
void test_curve_above_upper_bound(void);
void test_curve_negative_temp(void);
void test_curve_very_high_temp(void);
void test_curve_monotonic(void);

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    // SHA-256 tests
    RUN_TEST(test_sha256_empty_string);
    RUN_TEST(test_sha256_abc);
    RUN_TEST(test_sha256_two_blocks);
    RUN_TEST(test_sha256d_known);
    RUN_TEST(test_sha256_midstate);
    RUN_TEST(test_sha256_genesis_header);
    RUN_TEST(test_sha256_transform_words);
    RUN_TEST(test_sha256_transform_performance);

    // Work module tests
    RUN_TEST(test_hex_to_bytes);
    RUN_TEST(test_bytes_to_hex);
    RUN_TEST(test_hex_roundtrip);
    RUN_TEST(test_serialize_header_genesis);
    RUN_TEST(test_set_header_nonce);
    RUN_TEST(test_nbits_to_target_genesis);
    RUN_TEST(test_nbits_to_target_high_diff);
    RUN_TEST(test_meets_target_pass);
    RUN_TEST(test_meets_target_fail);
    RUN_TEST(test_meets_target_equal);
    RUN_TEST(test_build_coinbase_hash);
    RUN_TEST(test_build_merkle_root_no_branches);
    RUN_TEST(test_build_merkle_root_with_branches);
    RUN_TEST(test_decode_stratum_prevhash);

    // Integration tests
    RUN_TEST(test_block1_full_pipeline);
    RUN_TEST(test_block170_merkle_and_hash);
    RUN_TEST(test_decode_stratum_prevhash_real);
    RUN_TEST(test_stratum_pipeline_block1);
    RUN_TEST(test_difficulty_to_target_diff1);
    RUN_TEST(test_difficulty_to_target_easy);
    RUN_TEST(test_difficulty_to_target_hard);
    RUN_TEST(test_mining_round_trip_block1);
    RUN_TEST(test_mining_early_reject_byte_order);
    RUN_TEST(test_difficulty_target_meets_target_integration);
    RUN_TEST(test_version_rolling_mask_increment);

    // nv_config tests
    RUN_TEST(test_nv_config_init);
    RUN_TEST(test_nv_config_all_empty_before_provisioning);
    RUN_TEST(test_nv_config_not_provisioned_by_default);

    // CRC tests
    RUN_TEST(test_crc5_chain_inactive);
    RUN_TEST(test_crc5_reg_write_a8);
    RUN_TEST(test_crc5_reg_write_3c);
    RUN_TEST(test_crc5_set_address);
    RUN_TEST(test_crc16_false_standard);
    RUN_TEST(test_crc16_false_empty);

    // PLL tests
    RUN_TEST(test_pll_500mhz);
    RUN_TEST(test_pll_600mhz);
    RUN_TEST(test_pll_400mhz);
    RUN_TEST(test_pll_vdo_scale_high);
    RUN_TEST(test_pll_vdo_scale_low);
    RUN_TEST(test_pll_postdiv_byte);

    // BM1370 framing tests
    RUN_TEST(test_build_cmd_inactive);
    RUN_TEST(test_build_cmd_read_chipid);
    RUN_TEST(test_build_cmd_buffer_too_small);
    RUN_TEST(test_build_job_size);
    RUN_TEST(test_build_job_crc16);
    RUN_TEST(test_extract_job_from_header);
    RUN_TEST(test_parse_nonce_valid);
    RUN_TEST(test_parse_nonce_bad_preamble);
    RUN_TEST(test_parse_nonce_too_short);
    RUN_TEST(test_decode_job_id);
    RUN_TEST(test_decode_version_bits);
    RUN_TEST(test_decode_version_bits_max);

    // ASIC ticket mask tests
    RUN_TEST(test_asic_ticket_mask_diff_1);
    RUN_TEST(test_asic_ticket_mask_diff_2);
    RUN_TEST(test_asic_ticket_mask_diff_4);
    RUN_TEST(test_asic_ticket_mask_diff_8);
    RUN_TEST(test_asic_ticket_mask_diff_256);
    RUN_TEST(test_asic_ticket_mask_diff_512);
    RUN_TEST(test_asic_ticket_mask_diff_1024);
    RUN_TEST(test_asic_ticket_mask_diff_2048);
    RUN_TEST(test_asic_ticket_mask_diff_4096);
    RUN_TEST(test_asic_ticket_mask_non_power_of_2_700);
    RUN_TEST(test_asic_ticket_mask_non_power_of_2_513);
    RUN_TEST(test_asic_ticket_mask_non_power_of_2_1000);
    RUN_TEST(test_asic_ticket_mask_below_1);
    RUN_TEST(test_asic_ticket_mask_very_small);
    RUN_TEST(test_asic_ticket_mask_diff_16);
    RUN_TEST(test_asic_ticket_mask_diff_32);
    RUN_TEST(test_asic_ticket_mask_diff_64);
    RUN_TEST(test_asic_ticket_mask_diff_128);
    RUN_TEST(test_asic_ticket_mask_boundary_513_5);
    RUN_TEST(test_asic_ticket_mask_boundary_512_1);

    // Mining loop tests
    RUN_TEST(test_sw_backend_finds_block1_share);
    RUN_TEST(test_sw_backend_early_reject_low_diff);
    RUN_TEST(test_sw_backend_early_reject_high_diff);
    RUN_TEST(test_mine_nonce_range_counts);
    RUN_TEST(test_mine_nonce_range_stops_on_hit);
    RUN_TEST(test_mine_result_has_version_hex);
    RUN_TEST(test_mine_nonce_range_no_hit);
    RUN_TEST(test_pack_target_word0_diff1);
    RUN_TEST(test_pack_target_word0_easy_diff);
    RUN_TEST(test_pack_target_word0_hard_diff);
    RUN_TEST(test_build_block2_padding);
    RUN_TEST(test_package_result_no_version_rolling);
    RUN_TEST(test_package_result_version_rolling_submits_ver_bits);

    // HTTP utils tests
    RUN_TEST(test_url_decode_basic);
    RUN_TEST(test_url_decode_plus_as_space);
    RUN_TEST(test_url_decode_hex_decode);
    RUN_TEST(test_url_decode_missing_field);
    RUN_TEST(test_url_decode_truncation);
    RUN_TEST(test_url_decode_percent_at_end);
    RUN_TEST(test_url_decode_field_not_first);
    RUN_TEST(test_url_decode_empty_value);
    RUN_TEST(test_url_decode_field_at_end);

    // Stratum utils tests
    RUN_TEST(test_format_submit_no_version);
    RUN_TEST(test_format_submit_with_version);
    RUN_TEST(test_format_submit_null_version);
    RUN_TEST(test_format_submit_truncation);
    RUN_TEST(test_format_request_basic);
    RUN_TEST(test_format_request_truncation);
    RUN_TEST(test_format_request_newline);

    // Stats tests
    RUN_TEST(test_ema_seeds_on_first_sample);
    RUN_TEST(test_ema_converges);
    RUN_TEST(test_ema_decay);
    RUN_TEST(test_hash_to_difficulty_leading_zeros);
    RUN_TEST(test_hash_to_difficulty_diff1);
    RUN_TEST(test_hash_to_difficulty_easy);
    RUN_TEST(test_hash_to_difficulty_six_zeros);
    RUN_TEST(test_best_diff_only_increases);

    // Target validation tests
    RUN_TEST(test_difficulty_to_target_nan);
    RUN_TEST(test_difficulty_to_target_inf);
    RUN_TEST(test_difficulty_to_target_neg_inf);
    RUN_TEST(test_difficulty_to_target_negative);
    RUN_TEST(test_difficulty_to_target_zero);
    RUN_TEST(test_difficulty_to_target_tiny);
    RUN_TEST(test_difficulty_to_target_normal);
    RUN_TEST(test_is_target_valid_all_zero);
    RUN_TEST(test_is_target_valid_all_ff);
    RUN_TEST(test_is_target_valid_nonzero_msb31);
    RUN_TEST(test_is_target_valid_nonzero_msb30);
    RUN_TEST(test_is_target_valid_diff1);
    RUN_TEST(test_is_target_valid_diff512);
    RUN_TEST(test_is_target_valid_diff_001);

    // BM1368 tests
    RUN_TEST(test_bm1368_pll_fb_range);
    RUN_TEST(test_bm1368_pll_490mhz);
    RUN_TEST(test_bm1368_pll_vdo_scale);
    RUN_TEST(test_asic_ticket_mask_256_bm1368);

    // TPS546 decode tests
    RUN_TEST(test_ulinear16_typical_vout);
    RUN_TEST(test_ulinear16_zero);
    RUN_TEST(test_slinear11_positive_exp_neg);
    RUN_TEST(test_slinear11_negative_mantissa);
    RUN_TEST(test_slinear11_zero_mantissa);

    // EMC2101 fan curve tests
    RUN_TEST(test_curve_below_lower_bound);
    RUN_TEST(test_curve_at_lower_bound);
    RUN_TEST(test_curve_mid_segment_one);
    RUN_TEST(test_curve_at_segment_boundary);
    RUN_TEST(test_curve_mid_segment_two);
    RUN_TEST(test_curve_at_upper_bound);
    RUN_TEST(test_curve_above_upper_bound);
    RUN_TEST(test_curve_negative_temp);
    RUN_TEST(test_curve_very_high_temp);
    RUN_TEST(test_curve_monotonic);

    return UNITY_END();
}
