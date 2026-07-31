// Host test harness entry point. test_<module>_<behavior> functions live in
// sibling files; declared + RUN_TEST()'d manually below (no auto-discovery).
//
// tm_mining (TA-561v2 port): mining engine host tests -- SHA-256, share
// validation, work/header serialization, hashrate averaging, per-pool
// stats, the mine_nonce_range hot loop, and the tm_mining_producer
// (mining_gather/mining_desc) surface.
#include <unity.h>

// test_sha256.c
void test_sha256_empty_string(void);
void test_sha256_abc(void);
void test_sha256_two_blocks(void);
void test_sha256d_known(void);
void test_sha256_midstate(void);
void test_sha256_genesis_header(void);
void test_sha256_transform_words(void);
void test_sha256_transform_performance(void);
void test_sha256_build_abc_block(void);

// test_sha_self_test_gate.c
void test_sha256_sw_self_test_passes(void);
void test_mining_self_test_flag_default_false(void);
void test_mining_set_self_test_failed_flips_flag(void);
void test_sha256_check_abc_vector_accepts_correct_digest(void);
void test_sha256_check_abc_vector_rejects_wrong_digest(void);
void test_sha256_check_abc_vector_rejects_one_byte_flip(void);

// test_sha_overlap_hwrite_state.c
void test_mining_get_sha_overlap_state_default_unknown(void);
void test_mining_set_sha_overlap_safe_true_sets_safe(void);
void test_mining_set_sha_overlap_safe_false_sets_unsafe(void);
void test_mining_get_sha_hwrite_state_default_unknown(void);
void test_mining_set_sha_hwrite_safe_true_sets_safe(void);
void test_mining_set_sha_hwrite_safe_false_sets_unsafe(void);

// test_share_validate.c
void test_share_meets_network_target_genesis_block(void);
void test_share_meets_network_target_pool_share_fails(void);
void test_share_meets_network_target_exact_equals_target(void);
void test_share_meets_network_target_known_hit(void);
void test_share_meets_network_target_share_misses_network(void);
void test_share_meets_network_target_pool_diff_not_network(void);
void test_share_validate_invalid_target_rejects(void);
void test_share_validate_below_target_misses(void);
void test_share_validate_low_difficulty_just_under_floor_rejects(void);
void test_share_validate_low_difficulty_at_floor_accepts(void);
void test_share_validate_clean_valid_accept(void);

// test_work.c
void test_mining_hash_from_state_abc_vector(void);
void test_mining_hash_from_state_zero(void);
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
void test_decode_stratum_prevhash_truncated(void);
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
void test_package_result_round_trip_no_rolling(void);
void test_package_result_round_trip_with_rolling(void);

// test_mining_avg.c
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
void test_mining_pool_eff_tick_zero_delta(void);
void test_mining_pool_eff_tick_typical(void);
void test_mining_pool_eff_tick_sum_decrease_clamps_to_zero(void);

// test_mining.c
void test_sw_backend_finds_block1_share(void);
void test_sw_backend_early_reject_low_diff(void);
void test_sw_backend_early_reject_high_diff(void);
void test_mine_nonce_range_counts(void);
void test_mine_nonce_range_stops_on_hit(void);
void test_mine_nonce_range_no_hit(void);
void test_mine_result_has_version_hex(void);
void test_pack_target_word0_diff1(void);
void test_pack_target_word0_easy_diff(void);
void test_pack_target_word0_hard_diff(void);
void test_build_block2_padding(void);
void test_package_result_no_version_rolling(void);
void test_package_result_version_rolling_submits_ver_bits(void);
void test_mining_compute_pool_effective_hps_empty(void);
void test_mining_compute_pool_effective_hps_uptime_too_short(void);
void test_mining_compute_pool_effective_hps_typical(void);
void test_mining_compute_pool_effective_hps_diff1_share(void);
void test_mining_compute_pool_effective_hps_divide_by_zero_guard(void);
void test_mining_get_pool_effective_hashrate_host_stub(void);
void test_mining_get_pool_effective_rolling_host_stubs(void);
void test_pack_target_word0_exact_byte_order(void);
void test_share_reverify_block1_nonce(void);
void test_share_reverify_version_rolling(void);
void test_mining_efficiency_jth_known_case(void);
void test_mining_efficiency_jth_zero_hashrate(void);
void test_mining_efficiency_jth_negative_hashrate(void);
void test_mining_efficiency_jth_zero_power(void);
void test_mining_efficiency_jth_negative_power(void);
void test_mining_efficiency_jth_inverse_hashrate_scaling(void);
void test_sw_hash_nonce_rejects_over_target(void);
void test_block_found_cb_fires(void);
void test_block_found_cb_null_safe(void);
void test_mining_work_peek_default_false(void);
void test_mining_result_post_default_false(void);
void test_mining_queue_ops_bound_dispatches(void);
void test_mining_pause_gate_default_false(void);
void test_mining_pause_gate_bound(void);

// test_mining_pool_stats.c
void test_pool_stats_find_returns_existing_slot(void);
void test_pool_stats_case_insensitive_host_match(void);
void test_pool_stats_different_port_is_new_slot(void);
void test_pool_stats_alloc_into_empty_slot(void);
void test_pool_stats_lru_eviction_picks_lowest_last_seen(void);
void test_pool_stats_lru_eviction_skips_recent_slot(void);
void test_pool_stats_record_share_updates_matching_slot(void);
void test_pool_stats_record_block_increments_matching_slot(void);
void test_pool_stats_lifetime_blocks_survive_eviction(void);
void test_pool_stats_record_hashes_accumulates(void);
void test_pool_stats_null_slot_is_safe(void);
void test_pool_stats_slot_out_of_range_returns_null(void);
void test_pool_stats_find_or_alloc_null_host(void);
void test_pool_stats_init_runs_clean(void);
void test_pool_stats_reset_zeroes_all_state(void);

// test_mining_hotloop_sync.c
void test_mining_hotloop_finds_known_share(void);
void test_mining_hotloop_rejects_non_matching_nonce(void);

// test_tm_mining_producer.c
void test_mining_gather_unseeded_is_zeroed(void);
void test_mining_gather_round_trips_seeded_snapshot(void);
void test_mining_gather_round_trips_sha_self_test_failed_true(void);
void test_mining_gather_null_out_is_safe(void);
void test_mining_desc_snap_size_matches_struct(void);
void test_mining_desc_find_known_key(void);
void test_mining_desc_find_unknown_key(void);
void test_mining_desc_walk_matches_seeded_snapshot(void);


void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_empty_string);
    RUN_TEST(test_sha256_abc);
    RUN_TEST(test_sha256_two_blocks);
    RUN_TEST(test_sha256d_known);
    RUN_TEST(test_sha256_midstate);
    RUN_TEST(test_sha256_genesis_header);
    RUN_TEST(test_sha256_transform_words);
    RUN_TEST(test_sha256_transform_performance);
    RUN_TEST(test_sha256_build_abc_block);
    RUN_TEST(test_sha256_sw_self_test_passes);
    RUN_TEST(test_mining_self_test_flag_default_false);
    RUN_TEST(test_mining_set_self_test_failed_flips_flag);
    RUN_TEST(test_sha256_check_abc_vector_accepts_correct_digest);
    RUN_TEST(test_sha256_check_abc_vector_rejects_wrong_digest);
    RUN_TEST(test_sha256_check_abc_vector_rejects_one_byte_flip);
    RUN_TEST(test_mining_get_sha_overlap_state_default_unknown);
    RUN_TEST(test_mining_set_sha_overlap_safe_true_sets_safe);
    RUN_TEST(test_mining_set_sha_overlap_safe_false_sets_unsafe);
    RUN_TEST(test_mining_get_sha_hwrite_state_default_unknown);
    RUN_TEST(test_mining_set_sha_hwrite_safe_true_sets_safe);
    RUN_TEST(test_mining_set_sha_hwrite_safe_false_sets_unsafe);
    RUN_TEST(test_share_meets_network_target_genesis_block);
    RUN_TEST(test_share_meets_network_target_pool_share_fails);
    RUN_TEST(test_share_meets_network_target_exact_equals_target);
    RUN_TEST(test_share_meets_network_target_known_hit);
    RUN_TEST(test_share_meets_network_target_share_misses_network);
    RUN_TEST(test_share_meets_network_target_pool_diff_not_network);
    RUN_TEST(test_share_validate_invalid_target_rejects);
    RUN_TEST(test_share_validate_below_target_misses);
    RUN_TEST(test_share_validate_low_difficulty_just_under_floor_rejects);
    RUN_TEST(test_share_validate_low_difficulty_at_floor_accepts);
    RUN_TEST(test_share_validate_clean_valid_accept);
    RUN_TEST(test_mining_hash_from_state_abc_vector);
    RUN_TEST(test_mining_hash_from_state_zero);
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
    RUN_TEST(test_decode_stratum_prevhash_truncated);
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
    RUN_TEST(test_package_result_round_trip_no_rolling);
    RUN_TEST(test_package_result_round_trip_with_rolling);
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
    RUN_TEST(test_mining_pool_eff_tick_zero_delta);
    RUN_TEST(test_mining_pool_eff_tick_typical);
    RUN_TEST(test_mining_pool_eff_tick_sum_decrease_clamps_to_zero);
    RUN_TEST(test_sw_backend_finds_block1_share);
    RUN_TEST(test_sw_backend_early_reject_low_diff);
    RUN_TEST(test_sw_backend_early_reject_high_diff);
    RUN_TEST(test_mine_nonce_range_counts);
    RUN_TEST(test_mine_nonce_range_stops_on_hit);
    RUN_TEST(test_mine_nonce_range_no_hit);
    RUN_TEST(test_mine_result_has_version_hex);
    RUN_TEST(test_pack_target_word0_diff1);
    RUN_TEST(test_pack_target_word0_easy_diff);
    RUN_TEST(test_pack_target_word0_hard_diff);
    RUN_TEST(test_build_block2_padding);
    RUN_TEST(test_package_result_no_version_rolling);
    RUN_TEST(test_package_result_version_rolling_submits_ver_bits);
    RUN_TEST(test_mining_compute_pool_effective_hps_empty);
    RUN_TEST(test_mining_compute_pool_effective_hps_uptime_too_short);
    RUN_TEST(test_mining_compute_pool_effective_hps_typical);
    RUN_TEST(test_mining_compute_pool_effective_hps_diff1_share);
    RUN_TEST(test_mining_compute_pool_effective_hps_divide_by_zero_guard);
    RUN_TEST(test_mining_get_pool_effective_hashrate_host_stub);
    RUN_TEST(test_mining_get_pool_effective_rolling_host_stubs);
    RUN_TEST(test_pack_target_word0_exact_byte_order);
    RUN_TEST(test_share_reverify_block1_nonce);
    RUN_TEST(test_share_reverify_version_rolling);
    RUN_TEST(test_mining_efficiency_jth_known_case);
    RUN_TEST(test_mining_efficiency_jth_zero_hashrate);
    RUN_TEST(test_mining_efficiency_jth_negative_hashrate);
    RUN_TEST(test_mining_efficiency_jth_zero_power);
    RUN_TEST(test_mining_efficiency_jth_negative_power);
    RUN_TEST(test_mining_efficiency_jth_inverse_hashrate_scaling);
    RUN_TEST(test_sw_hash_nonce_rejects_over_target);
    RUN_TEST(test_block_found_cb_fires);
    RUN_TEST(test_block_found_cb_null_safe);
    RUN_TEST(test_mining_work_peek_default_false);
    RUN_TEST(test_mining_result_post_default_false);
    RUN_TEST(test_mining_queue_ops_bound_dispatches);
    RUN_TEST(test_mining_pause_gate_default_false);
    RUN_TEST(test_mining_pause_gate_bound);
    RUN_TEST(test_pool_stats_find_returns_existing_slot);
    RUN_TEST(test_pool_stats_case_insensitive_host_match);
    RUN_TEST(test_pool_stats_different_port_is_new_slot);
    RUN_TEST(test_pool_stats_alloc_into_empty_slot);
    RUN_TEST(test_pool_stats_lru_eviction_picks_lowest_last_seen);
    RUN_TEST(test_pool_stats_lru_eviction_skips_recent_slot);
    RUN_TEST(test_pool_stats_record_share_updates_matching_slot);
    RUN_TEST(test_pool_stats_record_block_increments_matching_slot);
    RUN_TEST(test_pool_stats_lifetime_blocks_survive_eviction);
    RUN_TEST(test_pool_stats_record_hashes_accumulates);
    RUN_TEST(test_pool_stats_null_slot_is_safe);
    RUN_TEST(test_pool_stats_slot_out_of_range_returns_null);
    RUN_TEST(test_pool_stats_find_or_alloc_null_host);
    RUN_TEST(test_pool_stats_init_runs_clean);
    RUN_TEST(test_pool_stats_reset_zeroes_all_state);
    RUN_TEST(test_mining_hotloop_finds_known_share);
    RUN_TEST(test_mining_hotloop_rejects_non_matching_nonce);
    RUN_TEST(test_mining_gather_unseeded_is_zeroed);
    RUN_TEST(test_mining_gather_round_trips_seeded_snapshot);
    RUN_TEST(test_mining_gather_round_trips_sha_self_test_failed_true);
    RUN_TEST(test_mining_gather_null_out_is_safe);
    RUN_TEST(test_mining_desc_snap_size_matches_struct);
    RUN_TEST(test_mining_desc_find_known_key);
    RUN_TEST(test_mining_desc_find_unknown_key);
    RUN_TEST(test_mining_desc_walk_matches_seeded_snapshot);
    return UNITY_END();
}
