#include "stratum_machine.h"
#include "work_build.h"   // decode_stratum_prevhash
#include "bb_str.h"        // bb_str_hex_to_bytes

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Small tok-recorder helpers -- every stratum string value in scope (hex,
// job ids, method names) is escape-free, so every span handed back by
// bb_serialize_json_tok_get_str() is a direct, durable slice of the caller's
// line buffer (CALLER_STABLE) -- see bb_serialize_json.h's STRING VALUE
// STORAGE note. That's what makes strtoul()/bb_str_hex_to_bytes() safe to
// call directly on the span pointer below: both stop at the first non-hex
// byte, which is always the JSON closing quote immediately following the
// span, even though the span itself is never NUL-terminated.
// ---------------------------------------------------------------------------

static void tok_copy_str(const bb_serialize_json_tok_recorder_t *rec, bb_serialize_json_tok_idx_t idx,
                         char *dst, size_t dst_cap)
{
    if (dst_cap == 0) return;
    const char *ptr;
    size_t len;
    if (!bb_serialize_json_tok_get_str(rec, idx, &ptr, &len)) {
        dst[0] = '\0';
        return;
    }
    size_t n = len < dst_cap - 1 ? len : dst_cap - 1;
    memcpy(dst, ptr, n);
    dst[n] = '\0';
}

static uint32_t tok_hex_u32(const bb_serialize_json_tok_recorder_t *rec, bb_serialize_json_tok_idx_t idx)
{
    const char *ptr;
    size_t len;
    if (!bb_serialize_json_tok_get_str(rec, idx, &ptr, &len)) return 0;
    return (uint32_t)strtoul(ptr, NULL, 16);
}

// ---------------------------------------------------------------------------
// JSON-RPC request builders
// ---------------------------------------------------------------------------

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
// Folded from the pre-rebuild stratum_utils.c -- generic request/response
// framing helpers (never JSON-tree-based, plain snprintf builders).
// ---------------------------------------------------------------------------

int format_submit_params(char *buf, size_t buf_size,
                         const char *wallet_addr, const char *worker_name,
                         const char *job_id, const char *extranonce2_hex,
                         const char *ntime_hex, const char *nonce_hex,
                         const char *version_hex)
{
    if (!buf || !buf_size || !wallet_addr || !worker_name || !job_id ||
        !extranonce2_hex || !ntime_hex || !nonce_hex) {
        return -1;
    }

    int result;
    if (version_hex && version_hex[0] != '\0') {
        // 6-field format with version
        result = snprintf(buf, buf_size,
                         "[\"%s.%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]",
                         wallet_addr, worker_name,
                         job_id, extranonce2_hex,
                         ntime_hex, nonce_hex, version_hex);
    } else {
        // 5-field format without version
        result = snprintf(buf, buf_size,
                         "[\"%s.%s\",\"%s\",\"%s\",\"%s\",\"%s\"]",
                         wallet_addr, worker_name,
                         job_id, extranonce2_hex,
                         ntime_hex, nonce_hex);
    }

    // Check for truncation: snprintf returns the number of characters that
    // would have been written if the buffer was large enough (excluding null)
    if (result < 0 || (size_t)result >= buf_size) {
        return -1;
    }

    return result;
}

int format_stratum_request(char *buf, size_t buf_size,
                           int id, const char *method, const char *params_json)
{
    if (!buf || !buf_size || !method || !params_json) {
        return -1;
    }

    int result = snprintf(buf, buf_size,
                         "{\"id\":%d,\"method\":\"%s\",\"params\":%s}\n",
                         id, method, params_json);

    if (result < 0 || (size_t)result >= buf_size) {
        return -1;
    }

    return result;
}

int stratum_parse_error_code(const bb_serialize_json_tok_recorder_t *rec,
                             bb_serialize_json_tok_idx_t error_item)
{
    if (!rec || error_item == BB_SERIALIZE_JSON_TOK_ABSENT) {
        return -1;
    }

    bb_serialize_json_tok_idx_t code_tok = BB_SERIALIZE_JSON_TOK_ABSENT;

    if (bb_serialize_json_tok_is_arr(rec, error_item)) {
        // Array form first: [code, "message", "data"].
        code_tok = bb_serialize_json_tok_arr_at(rec, error_item, 0);
    } else if (bb_serialize_json_tok_is_obj(rec, error_item)) {
        // Object form: {"code": N, ...}.
        code_tok = bb_serialize_json_tok_obj_get(rec, error_item, "code", 4);
    } else {
        return -1;
    }

    int64_t iv;
    if (bb_serialize_json_tok_get_i64(rec, code_tok, &iv)) {
        return (int)iv;
    }
    double dv;
    if (bb_serialize_json_tok_get_f64(rec, code_tok, &dv)) {
        return (int)dv;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Response handlers
// ---------------------------------------------------------------------------

bool stratum_machine_handle_configure_result(stratum_state_t *st,
                                             const bb_serialize_json_tok_recorder_t *rec,
                                             bb_serialize_json_tok_idx_t result)
{
    if (!st || !rec || result == BB_SERIALIZE_JSON_TOK_ABSENT) return false;
    if (!bb_serialize_json_tok_is_obj(rec, result)) return false;

    bb_serialize_json_tok_idx_t vr = bb_serialize_json_tok_obj_get(rec, result, "version-rolling", 15);
    bool vr_val = false;
    if (!bb_serialize_json_tok_get_bool(rec, vr, &vr_val) || !vr_val) {
        // Pool does not support version rolling -- non-fatal, mask stays 0.
        return false;
    }

    bb_serialize_json_tok_idx_t mask_j = bb_serialize_json_tok_obj_get(rec, result, "version-rolling.mask", 20);
    if (!bb_serialize_json_tok_is_str(rec, mask_j)) {
        return false;
    }

    st->version_mask = tok_hex_u32(rec, mask_j);
    return true;
}

bool stratum_machine_handle_subscribe_result(stratum_state_t *st,
                                             const bb_serialize_json_tok_recorder_t *rec,
                                             bb_serialize_json_tok_idx_t result)
{
    if (!st || !rec || result == BB_SERIALIZE_JSON_TOK_ABSENT) return false;
    if (!bb_serialize_json_tok_is_arr(rec, result) || bb_serialize_json_tok_arr_size(rec, result) < 3) {
        return false;
    }

    // result[1] = extranonce1 (hex string)
    bb_serialize_json_tok_idx_t en1 = bb_serialize_json_tok_arr_at(rec, result, 1);
    if (!bb_serialize_json_tok_is_str(rec, en1)) {
        return false;
    }

    const char *en1_ptr;
    size_t en1_len;
    bb_serialize_json_tok_get_str(rec, en1, &en1_ptr, &en1_len);
    if (en1_len > STRATUM_MAX_EXTRANONCE1_SIZE * 2) {
        return false;
    }

    tok_copy_str(rec, en1, st->extranonce1_hex, sizeof(st->extranonce1_hex));
    st->extranonce1_len = bb_str_hex_to_bytes(st->extranonce1_hex, st->extranonce1, STRATUM_MAX_EXTRANONCE1_SIZE);

    // result[2] = extranonce2_size -- bounded here: an unchecked value
    // silently drops every subsequent job further downstream in
    // build_work() (which guards its own coinbase buffer), so reject the
    // handshake cleanly instead of limping along with a bad size.
    bb_serialize_json_tok_idx_t en2sz = bb_serialize_json_tok_arr_at(rec, result, 2);
    int64_t sz;
    if (!bb_serialize_json_tok_get_i64(rec, en2sz, &sz)) {
        return false;
    }
    if (sz < 0 || sz > STRATUM_MAX_EXTRANONCE2_SIZE) {
        return false;
    }
    st->extranonce2_size = (int)sz;

    return true;
}

bool stratum_machine_handle_set_extranonce(stratum_state_t *st,
                                           const bb_serialize_json_tok_recorder_t *rec,
                                           bb_serialize_json_tok_idx_t params)
{
    if (!st || !rec || params == BB_SERIALIZE_JSON_TOK_ABSENT) return false;

    /* mining.set_extranonce params: [extranonce1_hex, extranonce2_size] */
    if (!bb_serialize_json_tok_is_arr(rec, params) || bb_serialize_json_tok_arr_size(rec, params) < 2) {
        return false;
    }

    bb_serialize_json_tok_idx_t en1 = bb_serialize_json_tok_arr_at(rec, params, 0);
    if (!bb_serialize_json_tok_is_str(rec, en1)) {
        return false;
    }
    const char *en1_ptr;
    size_t en1_len;
    bb_serialize_json_tok_get_str(rec, en1, &en1_ptr, &en1_len);
    if (en1_len > STRATUM_MAX_EXTRANONCE1_SIZE * 2) {
        return false;
    }

    bb_serialize_json_tok_idx_t en2sz = bb_serialize_json_tok_arr_at(rec, params, 1);
    int64_t new_en2_size;
    if (!bb_serialize_json_tok_get_i64(rec, en2sz, &new_en2_size)) {
        return false;
    }
    if (new_en2_size < 0 || new_en2_size > 16) {
        return false;
    }

    /* Apply atomically -- both fields validated. */
    tok_copy_str(rec, en1, st->extranonce1_hex, sizeof(st->extranonce1_hex));
    st->extranonce1_len = bb_str_hex_to_bytes(st->extranonce1_hex, st->extranonce1, STRATUM_MAX_EXTRANONCE1_SIZE);
    st->extranonce2_size = (int)new_en2_size;
    return true;
}

bool stratum_machine_handle_set_difficulty(stratum_state_t *st,
                                           const bb_serialize_json_tok_recorder_t *rec,
                                           bb_serialize_json_tok_idx_t params)
{
    if (!st || !rec || params == BB_SERIALIZE_JSON_TOK_ABSENT) return false;

    if (!bb_serialize_json_tok_is_arr(rec, params) || bb_serialize_json_tok_arr_size(rec, params) < 1) {
        return false;
    }

    bb_serialize_json_tok_idx_t diff = bb_serialize_json_tok_arr_at(rec, params, 0);
    double d;
    if (!bb_serialize_json_tok_get_f64(rec, diff, &d)) {
        return false;
    }

    if (!isfinite(d) || d <= 0.0) {
        return false;
    }

    st->difficulty = d;
    return true;
}

bool stratum_machine_handle_notify(stratum_state_t *st,
                                   const bb_serialize_json_tok_recorder_t *rec,
                                   bb_serialize_json_tok_idx_t params)
{
    if (!st || !rec || params == BB_SERIALIZE_JSON_TOK_ABSENT) return false;

    if (!bb_serialize_json_tok_is_arr(rec, params) || bb_serialize_json_tok_arr_size(rec, params) < 9) {
        return false;
    }

    bb_serialize_json_tok_idx_t job_id_j  = bb_serialize_json_tok_arr_at(rec, params, 0);
    bb_serialize_json_tok_idx_t prevhash_j = bb_serialize_json_tok_arr_at(rec, params, 1);
    bb_serialize_json_tok_idx_t coinb1_j  = bb_serialize_json_tok_arr_at(rec, params, 2);
    bb_serialize_json_tok_idx_t coinb2_j  = bb_serialize_json_tok_arr_at(rec, params, 3);
    bb_serialize_json_tok_idx_t merkle_j  = bb_serialize_json_tok_arr_at(rec, params, 4);
    bb_serialize_json_tok_idx_t version_j = bb_serialize_json_tok_arr_at(rec, params, 5);
    bb_serialize_json_tok_idx_t nbits_j   = bb_serialize_json_tok_arr_at(rec, params, 6);
    bb_serialize_json_tok_idx_t ntime_j   = bb_serialize_json_tok_arr_at(rec, params, 7);
    bb_serialize_json_tok_idx_t clean_j   = bb_serialize_json_tok_arr_at(rec, params, 8);

    if (job_id_j == BB_SERIALIZE_JSON_TOK_ABSENT || prevhash_j == BB_SERIALIZE_JSON_TOK_ABSENT ||
        coinb1_j == BB_SERIALIZE_JSON_TOK_ABSENT || coinb2_j == BB_SERIALIZE_JSON_TOK_ABSENT ||
        merkle_j == BB_SERIALIZE_JSON_TOK_ABSENT || version_j == BB_SERIALIZE_JSON_TOK_ABSENT ||
        nbits_j == BB_SERIALIZE_JSON_TOK_ABSENT || ntime_j == BB_SERIALIZE_JSON_TOK_ABSENT) {
        return false;
    }

    if (!bb_serialize_json_tok_is_str(rec, job_id_j) || !bb_serialize_json_tok_is_str(rec, prevhash_j) ||
        !bb_serialize_json_tok_is_str(rec, coinb1_j) || !bb_serialize_json_tok_is_str(rec, coinb2_j) ||
        !bb_serialize_json_tok_is_str(rec, version_j) || !bb_serialize_json_tok_is_str(rec, nbits_j) ||
        !bb_serialize_json_tok_is_str(rec, ntime_j)) {
        return false;
    }

    // job_id
    tok_copy_str(rec, job_id_j, st->job.job_id, sizeof(st->job.job_id));

    // prevhash -- stratum format (8 groups of 4 bytes, each reversed)
    const char *prevhash_ptr;
    size_t prevhash_len;
    bb_serialize_json_tok_get_str(rec, prevhash_j, &prevhash_ptr, &prevhash_len);
    decode_stratum_prevhash(prevhash_ptr, st->job.prevhash);

    // coinb1 / coinb2
    const char *coinb1_ptr;
    size_t coinb1_span_len;
    bb_serialize_json_tok_get_str(rec, coinb1_j, &coinb1_ptr, &coinb1_span_len);
    st->job.coinb1_len = bb_str_hex_to_bytes(coinb1_ptr, st->job.coinb1, STRATUM_MAX_COINB1_SIZE);

    const char *coinb2_ptr;
    size_t coinb2_span_len;
    bb_serialize_json_tok_get_str(rec, coinb2_j, &coinb2_ptr, &coinb2_span_len);
    st->job.coinb2_len = bb_str_hex_to_bytes(coinb2_ptr, st->job.coinb2, STRATUM_MAX_COINB2_SIZE);

    // merkle branches
    st->job.merkle_count = 0;
    if (bb_serialize_json_tok_is_arr(rec, merkle_j)) {
        int32_t branch_count = bb_serialize_json_tok_arr_size(rec, merkle_j);
        for (int i = 0; i < branch_count && i < STRATUM_MAX_MERKLE_BRANCHES; i++) {
            bb_serialize_json_tok_idx_t branch = bb_serialize_json_tok_arr_at(rec, merkle_j, (size_t)i);
            if (branch != BB_SERIALIZE_JSON_TOK_ABSENT && bb_serialize_json_tok_is_str(rec, branch)) {
                const char *bptr;
                size_t blen;
                bb_serialize_json_tok_get_str(rec, branch, &bptr, &blen);
                bb_str_hex_to_bytes(bptr, st->job.merkle_branches[st->job.merkle_count], 32);
                st->job.merkle_count++;
            }
        }
    }

    // version / nbits / ntime (hex strings -> uint32)
    st->job.version = tok_hex_u32(rec, version_j);
    st->job.nbits   = tok_hex_u32(rec, nbits_j);
    st->job.ntime   = tok_hex_u32(rec, ntime_j);

    bool clean = false;
    if (clean_j != BB_SERIALIZE_JSON_TOK_ABSENT) {
        bb_serialize_json_tok_get_bool(rec, clean_j, &clean);
    }
    st->job.clean_jobs = clean;

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
