#include "stratum_machine.h"
#include "stratum_utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

int stratum_machine_build_configure(char *buf, size_t n)
{
    if (!buf || !n) {
        return -1;
    }

    int result = snprintf(buf, n,
                         "[[\"version-rolling\"],"
                         "{\"version-rolling.mask\":\"1fffe000\","
                         "\"version-rolling.min-bit-count\":13}]");

    if (result < 0 || (size_t)result >= n) {
        return -1;
    }

    return result;
}

int stratum_machine_build_subscribe(char *buf, size_t n)
{
    if (!buf || !n) {
        return -1;
    }

    int result = snprintf(buf, n, "[\"TaipanMiner/0.1\"]");

    if (result < 0 || (size_t)result >= n) {
        return -1;
    }

    return result;
}

int stratum_machine_build_authorize(char *buf, size_t n,
                                    const char *wallet, const char *worker,
                                    const char *pass)
{
    if (!buf || !n || !wallet || !worker || !pass) {
        return -1;
    }

    int result = snprintf(buf, n, "[\"%s.%s\",\"%s\"]", wallet, worker, pass);

    if (result < 0 || (size_t)result >= n) {
        return -1;
    }

    return result;
}

int stratum_machine_build_keepalive(char *buf, size_t n, double difficulty)
{
    if (!buf || !n) {
        return -1;
    }

    int result = snprintf(buf, n, "[%.4f]", difficulty);

    if (result < 0 || (size_t)result >= n) {
        return -1;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Response handlers
// ---------------------------------------------------------------------------

bool stratum_machine_handle_configure_result(stratum_state_t *st, bb_json_t result)
{
    if (!st || !result) return false;

    bb_json_t vr = bb_json_obj_get_item(result, "version-rolling");
    if (!bb_json_item_is_true(vr)) {
        // Pool does not support version rolling -- non-fatal, mask stays 0.
        return false;
    }

    bb_json_t mask_j = bb_json_obj_get_item(result, "version-rolling.mask");
    if (!mask_j || !bb_json_item_is_string(mask_j)) {
        return false;
    }

    const char *mask_str = bb_json_item_get_string(mask_j);
    st->version_mask = (uint32_t)strtoul(mask_str, NULL, 16);
    return true;
}

bool stratum_machine_handle_subscribe_result(stratum_state_t *st, bb_json_t result)
{
    if (!st || !result) return false;

    if (!bb_json_item_is_array(result) || bb_json_arr_size(result) < 3) {
        return false;
    }

    // result[1] = extranonce1 (hex string)
    bb_json_t en1 = bb_json_arr_get_item(result, 1);
    if (!en1 || !bb_json_item_is_string(en1)) {
        return false;
    }

    const char *en1_str = bb_json_item_get_string(en1);

    size_t hex_len = strlen(en1_str);
    if (hex_len > STRATUM_MAX_EXTRANONCE1_SIZE * 2) {
        return false;
    }

    strncpy(st->extranonce1_hex, en1_str, sizeof(st->extranonce1_hex) - 1);
    st->extranonce1_hex[sizeof(st->extranonce1_hex) - 1] = '\0';
    st->extranonce1_len = hex_to_bytes(st->extranonce1_hex, st->extranonce1, STRATUM_MAX_EXTRANONCE1_SIZE);

    // result[2] = extranonce2_size
    bb_json_t en2sz = bb_json_arr_get_item(result, 2);
    if (bb_json_item_is_number(en2sz)) {
        st->extranonce2_size = bb_json_item_get_int(en2sz);
    }

    return true;
}

bool stratum_machine_handle_set_extranonce(stratum_state_t *st, bb_json_t params)
{
    if (!st || !params) return false;

    /* mining.set_extranonce params: [extranonce1_hex, extranonce2_size] */
    if (!bb_json_item_is_array(params) || bb_json_arr_size(params) < 2) {
        return false;
    }

    bb_json_t en1 = bb_json_arr_get_item(params, 0);
    if (!en1 || !bb_json_item_is_string(en1)) {
        return false;
    }
    const char *en1_str = bb_json_item_get_string(en1);
    size_t hex_len = strlen(en1_str);
    if (hex_len > STRATUM_MAX_EXTRANONCE1_SIZE * 2) {
        return false;
    }

    bb_json_t en2sz = bb_json_arr_get_item(params, 1);
    if (!en2sz || !bb_json_item_is_number(en2sz)) {
        return false;
    }
    int new_en2_size = bb_json_item_get_int(en2sz);
    if (new_en2_size < 0 || new_en2_size > 16) {
        return false;
    }

    /* Apply atomically -- both fields validated. */
    strncpy(st->extranonce1_hex, en1_str, sizeof(st->extranonce1_hex) - 1);
    st->extranonce1_hex[sizeof(st->extranonce1_hex) - 1] = '\0';
    st->extranonce1_len = hex_to_bytes(st->extranonce1_hex, st->extranonce1, STRATUM_MAX_EXTRANONCE1_SIZE);
    st->extranonce2_size = new_en2_size;
    return true;
}

bool stratum_machine_handle_set_difficulty(stratum_state_t *st, bb_json_t params)
{
    if (!st || !params) return false;

    if (!bb_json_item_is_array(params) || bb_json_arr_size(params) < 1) {
        return false;
    }

    bb_json_t diff = bb_json_arr_get_item(params, 0);
    if (!diff || !bb_json_item_is_number(diff)) {
        return false;
    }

    double d = bb_json_item_get_double(diff);

    if (!isfinite(d) || d <= 0.0) {
        return false;
    }

    st->difficulty = d;
    return true;
}

bool stratum_machine_handle_notify(stratum_state_t *st, bb_json_t params)
{
    if (!st || !params) return false;

    if (!bb_json_item_is_array(params) || bb_json_arr_size(params) < 9) {
        return false;
    }

    bb_json_t job_id_j  = bb_json_arr_get_item(params, 0);
    bb_json_t prevhash_j = bb_json_arr_get_item(params, 1);
    bb_json_t coinb1_j  = bb_json_arr_get_item(params, 2);
    bb_json_t coinb2_j  = bb_json_arr_get_item(params, 3);
    bb_json_t merkle_j  = bb_json_arr_get_item(params, 4);
    bb_json_t version_j = bb_json_arr_get_item(params, 5);
    bb_json_t nbits_j   = bb_json_arr_get_item(params, 6);
    bb_json_t ntime_j   = bb_json_arr_get_item(params, 7);
    bb_json_t clean_j   = bb_json_arr_get_item(params, 8);

    if (!job_id_j || !prevhash_j || !coinb1_j || !coinb2_j ||
        !merkle_j || !version_j || !nbits_j || !ntime_j) {
        return false;
    }

    if (!bb_json_item_is_string(job_id_j) || !bb_json_item_is_string(prevhash_j) ||
        !bb_json_item_is_string(coinb1_j) || !bb_json_item_is_string(coinb2_j) ||
        !bb_json_item_is_string(version_j) || !bb_json_item_is_string(nbits_j) ||
        !bb_json_item_is_string(ntime_j)) {
        return false;
    }

    // job_id
    strncpy(st->job.job_id, bb_json_item_get_string(job_id_j), sizeof(st->job.job_id) - 1);
    st->job.job_id[sizeof(st->job.job_id) - 1] = '\0';

    // prevhash -- stratum format (8 groups of 4 bytes, each reversed)
    decode_stratum_prevhash(bb_json_item_get_string(prevhash_j), st->job.prevhash);

    // coinb1 / coinb2
    st->job.coinb1_len = hex_to_bytes(bb_json_item_get_string(coinb1_j), st->job.coinb1, STRATUM_MAX_COINB1_SIZE);
    st->job.coinb2_len = hex_to_bytes(bb_json_item_get_string(coinb2_j), st->job.coinb2, STRATUM_MAX_COINB2_SIZE);

    // merkle branches
    st->job.merkle_count = 0;
    int branch_count = bb_json_arr_size(merkle_j);
    for (int i = 0; i < branch_count && i < STRATUM_MAX_MERKLE_BRANCHES; i++) {
        bb_json_t branch = bb_json_arr_get_item(merkle_j, i);
        if (branch && bb_json_item_is_string(branch)) {
            hex_to_bytes(bb_json_item_get_string(branch), st->job.merkle_branches[i], 32);
            st->job.merkle_count++;
        }
    }

    // version / nbits / ntime (hex strings -> uint32)
    st->job.version = (uint32_t)strtoul(bb_json_item_get_string(version_j), NULL, 16);
    st->job.nbits   = (uint32_t)strtoul(bb_json_item_get_string(nbits_j),   NULL, 16);
    st->job.ntime   = (uint32_t)strtoul(bb_json_item_get_string(ntime_j),   NULL, 16);
    st->job.clean_jobs = clean_j ? bb_json_item_is_true(clean_j) : false;

    // Reset extranonce2 for new job
    st->extranonce2 = 0;

    return true;
}

stratum_reject_kind_t stratum_machine_classify_reject(int code)
{
    switch (code) {
        case 21: return STRATUM_REJECT_JOB_NOT_FOUND;
        case 22: return STRATUM_REJECT_DUPLICATE;
        case 23: return STRATUM_REJECT_LOW_DIFFICULTY;
        case 25: return STRATUM_REJECT_STALE_PREVHASH;
        default: return STRATUM_REJECT_OTHER;
    }
}

bool stratum_should_kick_wifi(int fail_count, bool has_ip)
{
    return has_ip && (fail_count >= STRATUM_WIFI_KICK_THRESHOLD);
}
