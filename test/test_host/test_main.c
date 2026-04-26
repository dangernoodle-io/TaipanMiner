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

// Forward declarations from test_taipan_config_hostname.c
void test_valid_hostname_single_char(void);
void test_valid_hostname_lowercase_digits_hyphen(void);
void test_valid_hostname_mixed(void);
void test_valid_hostname_max_length(void);
void test_invalid_hostname_empty(void);
void test_invalid_hostname_leading_hyphen(void);
void test_invalid_hostname_trailing_hyphen(void);
void test_invalid_hostname_uppercase(void);
void test_invalid_hostname_underscore(void);
void test_invalid_hostname_dot(void);
void test_invalid_hostname_too_long(void);
void test_hostname_persists_after_set(void);

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
void test_curve_at_60c(void);
void test_curve_mid_segment_two(void);
void test_curve_at_75c(void);
void test_curve_mid_segment_three(void);
void test_curve_at_upper_bound(void);
void test_curve_above_upper_bound(void);
void test_curve_negative_temp(void);
void test_curve_very_high_temp(void);
void test_curve_monotonic(void);

// Forward declarations from test_ota_validator.c
void test_ota_validator_init(void);
void test_ota_validator_on_stratum_authorized_not_pending(void);
void test_ota_validator_on_stratum_authorized_pending_first_time(void);
void test_ota_validator_on_stratum_authorized_pending_already_armed(void);
void test_ota_validator_on_share_accepted_not_pending(void);
void test_ota_validator_on_share_accepted_pending_with_armed_timer(void);
void test_ota_validator_on_share_accepted_pending_without_timer(void);
void test_ota_validator_on_timer_fired_not_pending(void);
void test_ota_validator_on_timer_fired_pending(void);
void test_ota_validator_happy_path_share_accepted(void);
void test_ota_validator_happy_path_timeout(void);

// Forward declarations from test_asic_pause_coalesce.c
void test_asic_pause_idle_when_not_pending_and_not_quiesced(void);
void test_asic_pause_first_pause_quiesces_and_acks(void);
void test_asic_pause_second_pause_only_acks(void);
void test_asic_pause_resume_when_clear_and_quiesced(void);
void test_asic_pause_full_check_install_resume_sequence(void);

// TA-234: mining_pause_state
void test_mining_pause_state_init(void);
void test_mining_pause_state_request(void);
void test_mining_pause_state_on_check_not_requested(void);
void test_mining_pause_state_on_check_requested(void);
void test_mining_pause_state_on_resume_when_active(void);
void test_mining_pause_state_on_resume_when_not_active(void);
void test_mining_pause_state_on_resumed(void);
void test_mining_pause_state_on_ack_timeout(void);
void test_mining_pause_state_full_happy_path(void);
void test_mining_pause_state_resume_before_check(void);

// TA-234: asic_nonce_dedup
void test_nonce_dedup_fresh_state_has_no_dups(void);
void test_nonce_dedup_immediate_redundant_insert_returns_true(void);
void test_nonce_dedup_different_job_id_is_not_dup(void);
void test_nonce_dedup_different_nonce_is_not_dup(void);
void test_nonce_dedup_different_ver_is_not_dup(void);
void test_nonce_dedup_ring_wraparound_evicts_oldest(void);
void test_nonce_dedup_reset_clears_all(void);
void test_nonce_dedup_next_idx_advances_cyclically(void);

// TA-234: asic_metric_avg
void test_avg_nan_safe_empty_all_nan(void);
void test_avg_nan_safe_single_value(void);
void test_avg_nan_safe_partial_nan(void);
void test_avg_nan_safe_all_populated(void);
void test_update_warmup_1m(void);
void test_update_full_1m_window(void);
void test_update_step_change(void);
void test_update_ring_wraparound(void);
void test_update_10m_blend_formula(void);
void test_update_1h_accumulation(void);
void test_update_with_zero_samples(void);
void test_update_mixed_values(void);

// TA-234: stratum_backoff
void test_stratum_backoff_init(void);
void test_stratum_backoff_first_fail_sleeps_initial_then_doubles(void);
void test_stratum_backoff_progression_to_kick(void);
void test_stratum_backoff_caps_at_60s(void);
void test_stratum_backoff_reset_on_success(void);
void test_stratum_backoff_post_kick_restarts_clean(void);

// TA-234: asic_drop_detect
void test_drop_detect_accepts_below_sanity(void);
void test_drop_detect_rejects_at_or_above_sanity(void);
void test_drop_detect_first_warn_fires_immediately(void);
void test_drop_detect_warn_cooldown_suppresses(void);
void test_drop_detect_warn_after_cooldown_elapsed(void);
void test_drop_detect_domain_smaller_cap(void);
void test_drop_detect_zero_cooldown_always_warns(void);

// TA-238: asic_drop_log
void test_drop_log_empty_snapshot(void);
void test_drop_log_single_push_readback(void);
void test_drop_log_fill_to_cap_newest_first(void);
void test_drop_log_wrap_drops_oldest(void);
void test_drop_log_total_written_monotonic(void);
void test_drop_log_max_out_smaller_than_cap(void);

// TA-234: asic_chip_routing
void test_chip_routing_single_chip_addr_zero(void);
void test_chip_routing_single_chip_any_addr_returns_zero(void);
void test_chip_routing_two_chip_addr_zero(void);
void test_chip_routing_two_chip_addr_below_boundary(void);
void test_chip_routing_two_chip_addr_at_boundary(void);
void test_chip_routing_two_chip_addr_top(void);
void test_chip_routing_four_chips(void);
void test_chip_routing_invalid_chip_count_zero(void);
void test_chip_routing_invalid_chip_count_negative(void);
void test_chip_routing_invalid_chip_count_too_large(void);

// TA-234: partition_fixup_decision
void test_pfd_skip_when_expected_empty(void);
void test_pfd_skip_when_expected_too_short(void);
void test_pfd_skip_when_live_unreadable(void);
void test_pfd_skip_when_table_matches(void);
void test_pfd_rewrite_only_when_running_at_correct_addr(void);
void test_pfd_copy_and_rewrite_when_running_at_wrong_addr(void);
void test_pfd_running_addr_zero_treated_as_wrong(void);

// TA-234: stratum_watchdogs
void test_stratum_watchdog_job_drought_never_observed(void);
void test_stratum_watchdog_job_drought_below_threshold(void);
void test_stratum_watchdog_job_drought_at_threshold(void);
void test_stratum_watchdog_job_drought_above_threshold(void);
void test_stratum_watchdog_job_drought_wraparound(void);
void test_stratum_watchdog_share_drought_both_zero(void);
void test_stratum_watchdog_share_drought_only_last_share_below_threshold(void);
void test_stratum_watchdog_share_drought_session_start_at_threshold(void);
void test_stratum_watchdog_share_drought_only_last_share_above_threshold(void);
void test_stratum_watchdog_share_drought_prefers_last_share(void);
void test_stratum_watchdog_share_drought_falls_back_to_session_start(void);
void test_stratum_watchdog_share_drought_last_share_overrides_old_session(void);
void test_stratum_watchdog_keepalive_never_transmitted(void);
void test_stratum_watchdog_keepalive_below_threshold(void);
void test_stratum_watchdog_keepalive_at_threshold(void);
void test_stratum_watchdog_keepalive_above_threshold(void);
void test_stratum_watchdog_keepalive_wraparound(void);

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

    // taipan_config_hostname tests
    RUN_TEST(test_valid_hostname_single_char);
    RUN_TEST(test_valid_hostname_lowercase_digits_hyphen);
    RUN_TEST(test_valid_hostname_mixed);
    RUN_TEST(test_valid_hostname_max_length);
    RUN_TEST(test_invalid_hostname_empty);
    RUN_TEST(test_invalid_hostname_leading_hyphen);
    RUN_TEST(test_invalid_hostname_trailing_hyphen);
    RUN_TEST(test_invalid_hostname_uppercase);
    RUN_TEST(test_invalid_hostname_underscore);
    RUN_TEST(test_invalid_hostname_dot);
    RUN_TEST(test_invalid_hostname_too_long);
    RUN_TEST(test_hostname_persists_after_set);

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
    RUN_TEST(test_curve_at_60c);
    RUN_TEST(test_curve_mid_segment_two);
    RUN_TEST(test_curve_at_75c);
    RUN_TEST(test_curve_mid_segment_three);
    RUN_TEST(test_curve_at_upper_bound);
    RUN_TEST(test_curve_above_upper_bound);
    RUN_TEST(test_curve_negative_temp);
    RUN_TEST(test_curve_very_high_temp);
    RUN_TEST(test_curve_monotonic);

    // OTA validator state machine tests
    RUN_TEST(test_ota_validator_init);
    RUN_TEST(test_ota_validator_on_stratum_authorized_not_pending);
    RUN_TEST(test_ota_validator_on_stratum_authorized_pending_first_time);
    RUN_TEST(test_ota_validator_on_stratum_authorized_pending_already_armed);
    RUN_TEST(test_ota_validator_on_share_accepted_not_pending);
    RUN_TEST(test_ota_validator_on_share_accepted_pending_with_armed_timer);
    RUN_TEST(test_ota_validator_on_share_accepted_pending_without_timer);
    RUN_TEST(test_ota_validator_on_timer_fired_not_pending);
    RUN_TEST(test_ota_validator_on_timer_fired_pending);
    RUN_TEST(test_ota_validator_happy_path_share_accepted);
    RUN_TEST(test_ota_validator_happy_path_timeout);

    // ASIC pause/resume coalesce tests
    RUN_TEST(test_asic_pause_idle_when_not_pending_and_not_quiesced);
    RUN_TEST(test_asic_pause_first_pause_quiesces_and_acks);
    RUN_TEST(test_asic_pause_second_pause_only_acks);
    RUN_TEST(test_asic_pause_resume_when_clear_and_quiesced);
    RUN_TEST(test_asic_pause_full_check_install_resume_sequence);

    // TA-234: mining_pause_state tests
    RUN_TEST(test_mining_pause_state_init);
    RUN_TEST(test_mining_pause_state_request);
    RUN_TEST(test_mining_pause_state_on_check_not_requested);
    RUN_TEST(test_mining_pause_state_on_check_requested);
    RUN_TEST(test_mining_pause_state_on_resume_when_active);
    RUN_TEST(test_mining_pause_state_on_resume_when_not_active);
    RUN_TEST(test_mining_pause_state_on_resumed);
    RUN_TEST(test_mining_pause_state_on_ack_timeout);
    RUN_TEST(test_mining_pause_state_full_happy_path);
    RUN_TEST(test_mining_pause_state_resume_before_check);

    // TA-234: asic_nonce_dedup tests
    RUN_TEST(test_nonce_dedup_fresh_state_has_no_dups);
    RUN_TEST(test_nonce_dedup_immediate_redundant_insert_returns_true);
    RUN_TEST(test_nonce_dedup_different_job_id_is_not_dup);
    RUN_TEST(test_nonce_dedup_different_nonce_is_not_dup);
    RUN_TEST(test_nonce_dedup_different_ver_is_not_dup);
    RUN_TEST(test_nonce_dedup_ring_wraparound_evicts_oldest);
    RUN_TEST(test_nonce_dedup_reset_clears_all);
    RUN_TEST(test_nonce_dedup_next_idx_advances_cyclically);

    // TA-234: asic_metric_avg tests
    RUN_TEST(test_avg_nan_safe_empty_all_nan);
    RUN_TEST(test_avg_nan_safe_single_value);
    RUN_TEST(test_avg_nan_safe_partial_nan);
    RUN_TEST(test_avg_nan_safe_all_populated);
    RUN_TEST(test_update_warmup_1m);
    RUN_TEST(test_update_full_1m_window);
    RUN_TEST(test_update_step_change);
    RUN_TEST(test_update_ring_wraparound);
    RUN_TEST(test_update_10m_blend_formula);
    RUN_TEST(test_update_1h_accumulation);
    RUN_TEST(test_update_with_zero_samples);
    RUN_TEST(test_update_mixed_values);

    // TA-234: stratum_backoff tests
    RUN_TEST(test_stratum_backoff_init);
    RUN_TEST(test_stratum_backoff_first_fail_sleeps_initial_then_doubles);
    RUN_TEST(test_stratum_backoff_progression_to_kick);
    RUN_TEST(test_stratum_backoff_caps_at_60s);
    RUN_TEST(test_stratum_backoff_reset_on_success);
    RUN_TEST(test_stratum_backoff_post_kick_restarts_clean);

    // TA-234: asic_drop_detect tests
    RUN_TEST(test_drop_detect_accepts_below_sanity);
    RUN_TEST(test_drop_detect_rejects_at_or_above_sanity);
    RUN_TEST(test_drop_detect_first_warn_fires_immediately);
    RUN_TEST(test_drop_detect_warn_cooldown_suppresses);
    RUN_TEST(test_drop_detect_warn_after_cooldown_elapsed);
    RUN_TEST(test_drop_detect_domain_smaller_cap);
    RUN_TEST(test_drop_detect_zero_cooldown_always_warns);

    // TA-238: asic_drop_log tests
    RUN_TEST(test_drop_log_empty_snapshot);
    RUN_TEST(test_drop_log_single_push_readback);
    RUN_TEST(test_drop_log_fill_to_cap_newest_first);
    RUN_TEST(test_drop_log_wrap_drops_oldest);
    RUN_TEST(test_drop_log_total_written_monotonic);
    RUN_TEST(test_drop_log_max_out_smaller_than_cap);

    // TA-234: asic_chip_routing tests
    RUN_TEST(test_chip_routing_single_chip_addr_zero);
    RUN_TEST(test_chip_routing_single_chip_any_addr_returns_zero);
    RUN_TEST(test_chip_routing_two_chip_addr_zero);
    RUN_TEST(test_chip_routing_two_chip_addr_below_boundary);
    RUN_TEST(test_chip_routing_two_chip_addr_at_boundary);
    RUN_TEST(test_chip_routing_two_chip_addr_top);
    RUN_TEST(test_chip_routing_four_chips);
    RUN_TEST(test_chip_routing_invalid_chip_count_zero);
    RUN_TEST(test_chip_routing_invalid_chip_count_negative);
    RUN_TEST(test_chip_routing_invalid_chip_count_too_large);

    // TA-234: partition_fixup_decision tests
    RUN_TEST(test_pfd_skip_when_expected_empty);
    RUN_TEST(test_pfd_skip_when_expected_too_short);
    RUN_TEST(test_pfd_skip_when_live_unreadable);
    RUN_TEST(test_pfd_skip_when_table_matches);
    RUN_TEST(test_pfd_rewrite_only_when_running_at_correct_addr);
    RUN_TEST(test_pfd_copy_and_rewrite_when_running_at_wrong_addr);
    RUN_TEST(test_pfd_running_addr_zero_treated_as_wrong);

    // TA-234: stratum_watchdogs tests
    RUN_TEST(test_stratum_watchdog_job_drought_never_observed);
    RUN_TEST(test_stratum_watchdog_job_drought_below_threshold);
    RUN_TEST(test_stratum_watchdog_job_drought_at_threshold);
    RUN_TEST(test_stratum_watchdog_job_drought_above_threshold);
    RUN_TEST(test_stratum_watchdog_job_drought_wraparound);
    RUN_TEST(test_stratum_watchdog_share_drought_both_zero);
    RUN_TEST(test_stratum_watchdog_share_drought_only_last_share_below_threshold);
    RUN_TEST(test_stratum_watchdog_share_drought_session_start_at_threshold);
    RUN_TEST(test_stratum_watchdog_share_drought_only_last_share_above_threshold);
    RUN_TEST(test_stratum_watchdog_share_drought_prefers_last_share);
    RUN_TEST(test_stratum_watchdog_share_drought_falls_back_to_session_start);
    RUN_TEST(test_stratum_watchdog_share_drought_last_share_overrides_old_session);
    RUN_TEST(test_stratum_watchdog_keepalive_never_transmitted);
    RUN_TEST(test_stratum_watchdog_keepalive_below_threshold);
    RUN_TEST(test_stratum_watchdog_keepalive_at_threshold);
    RUN_TEST(test_stratum_watchdog_keepalive_above_threshold);
    RUN_TEST(test_stratum_watchdog_keepalive_wraparound);

    return UNITY_END();
}
