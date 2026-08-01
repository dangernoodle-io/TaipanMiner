// bb_serialize_json tok-recorder sizing/robustness tests specific to
// stratum's usage: the worst-case mining.notify (16 merkle branches, the
// pool count as described in bb_serialize_json.h's
// BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP doc comment) must fit the default
// pool; a malformed line must fail the scan cleanly (no partial state
// leaked into a caller); and clean_jobs' absent-vs-false distinction must
// be observable via stratum_machine_handle_notify().
#include "unity.h"
#include "stratum_machine.h"
#include "bb_serialize_json.h"
#include <string.h>
#include <stdio.h>

static bb_serialize_json_tok_idx_t scan_json(const char *json, size_t len,
                                             bb_serialize_json_tok_recorder_t *rec,
                                             bb_serialize_json_tok_t *pool, size_t pool_cap)
{
    if (bb_serialize_json_tok_recorder_init(rec, json, len, pool, pool_cap, NULL, 0) != BB_OK) {
        return BB_SERIALIZE_JSON_TOK_ABSENT;
    }
    bb_serialize_json_ingest_t ingest = bb_serialize_json_tok_recorder_ingest(rec);
    if (bb_serialize_json_scan_bounded(json, len, &ingest) != BB_OK) {
        return BB_SERIALIZE_JSON_TOK_ABSENT;
    }
    return bb_serialize_json_tok_root(rec);
}

// Builds a mining.notify line with `branch_count` 32-byte-hex merkle
// branches -- mirrors a real pool's worst observed shape.
static size_t build_notify_line(char *out, size_t out_cap, int branch_count, bool include_clean_jobs)
{
    size_t n = 0;
    n += snprintf(out + n, out_cap - n,
                 "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                 "[\"job-worst-case\","
                 "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
                 "\"deadbeef\",\"cafecafe\",[");
    for (int i = 0; i < branch_count; i++) {
        n += snprintf(out + n, out_cap - n, "%s\"%064x\"", i == 0 ? "" : ",", i);
    }
    n += snprintf(out + n, out_cap - n, "],\"20000000\",\"1a0392a3\",\"67b1c400\"");
    if (include_clean_jobs) {
        n += snprintf(out + n, out_cap - n, ",true");
    }
    n += snprintf(out + n, out_cap - n, "]}");
    return n;
}

void test_json_ingest_worst_case_notify_fits_default_pool(void)
{
    char line[4096];
    size_t len = build_notify_line(line, sizeof(line), STRATUM_MAX_MERKLE_BRANCHES, true);
    TEST_ASSERT_LESS_THAN(sizeof(line), len);

    bb_serialize_json_tok_t pool[BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t root = scan_json(line, len, &rec, pool, BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP);

    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, root);
    TEST_ASSERT_TRUE(bb_serialize_json_tok_is_obj(&rec, root));

    bb_serialize_json_tok_idx_t params = bb_serialize_json_tok_obj_get(&rec, root, "params", 6);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    bool ok = stratum_machine_handle_notify(&st, &rec, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(STRATUM_MAX_MERKLE_BRANCHES, (int)st.job.merkle_count);
}

void test_json_ingest_smaller_pool_fails_cleanly_on_oversized_document(void)
{
    // A pool sized far below the worst case must fail the scan (NO_SPACE),
    // not corrupt/partially-populate the recorder.
    char line[4096];
    size_t len = build_notify_line(line, sizeof(line), STRATUM_MAX_MERKLE_BRANCHES, true);

    bb_serialize_json_tok_t pool[4];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t root = scan_json(line, len, &rec, pool, 4);

    TEST_ASSERT_EQUAL_INT(BB_SERIALIZE_JSON_TOK_ABSENT, root);
}

void test_json_ingest_malformed_line_fails_cleanly(void)
{
    const char *malformed = "{\"method\":\"mining.notify\",\"params\":[unterminated";
    size_t len = strlen(malformed);

    bb_serialize_json_tok_t pool[BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t root = scan_json(malformed, len, &rec, pool, BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP);

    TEST_ASSERT_EQUAL_INT(BB_SERIALIZE_JSON_TOK_ABSENT, root);
}

void test_json_ingest_truncated_line_fails_cleanly(void)
{
    const char *truncated = "{\"method\":\"mining.notify\",\"params\":[\"job-1\"";
    size_t len = strlen(truncated);

    bb_serialize_json_tok_t pool[BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t root = scan_json(truncated, len, &rec, pool, BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP);

    TEST_ASSERT_EQUAL_INT(BB_SERIALIZE_JSON_TOK_ABSENT, root);
}

// clean_jobs "absent" at the tok-recorder navigation layer (an out-of-range
// arr_at() index -- see bb_serialize_json_tok_idx_t's ABSENT-sentinel doc
// comment in bb_serialize_json.h) vs present-and-false. Note:
// stratum_machine_handle_notify() itself REQUIRES >=9 elements (matching the
// pre-rebuild bb_json handler's identical `arr_size(params) < 9` check) --
// a real mining.notify with a genuinely absent clean_jobs element (an
// 8-element params array) is rejected by the handler outright, so this
// "absent" case is exercised directly against the tok-recorder API instead
// of through the handler, which is where the ABSENT sentinel's safe-no-op
// contract actually matters (e.g. a future optional trailing field).
void test_json_ingest_clean_jobs_absent_defaults_false(void)
{
    const char *line = "[\"job-1\",\"deadbeef\"]";  // only 2 elements -- index 8 doesn't exist
    size_t len = strlen(line);

    bb_serialize_json_tok_t pool[BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t arr = scan_json(line, len, &rec, pool, BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, arr);
    TEST_ASSERT_EQUAL_INT(2, bb_serialize_json_tok_arr_size(&rec, arr));

    bb_serialize_json_tok_idx_t clean_tok = bb_serialize_json_tok_arr_at(&rec, arr, 8);
    TEST_ASSERT_EQUAL_INT(BB_SERIALIZE_JSON_TOK_ABSENT, clean_tok);

    // get_bool on the ABSENT sentinel is a safe no-op: returns false
    // (not-found) and never touches *out.
    bool v = true;
    TEST_ASSERT_FALSE(bb_serialize_json_tok_get_bool(&rec, clean_tok, &v));
    TEST_ASSERT_TRUE(v);  // untouched -- proves get_bool never wrote to it
}

void test_json_ingest_clean_jobs_present_false_is_distinct_from_absent(void)
{
    const char *line =
        "[\"job-1\",\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\",\"cafecafe\",[],\"20000000\",\"1a0392a3\",\"67b1c400\",false]";
    size_t len = strlen(line);

    bb_serialize_json_tok_t pool[BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json(line, len, &rec, pool, BB_SERIALIZE_JSON_TOK_POOL_DEFAULT_CAP);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);
    TEST_ASSERT_EQUAL_INT(9, bb_serialize_json_tok_arr_size(&rec, params));

    bb_serialize_json_tok_idx_t clean_tok = bb_serialize_json_tok_arr_at(&rec, params, 8);
    TEST_ASSERT_TRUE(bb_serialize_json_tok_is_bool(&rec, clean_tok));
    bool v = true;
    TEST_ASSERT_TRUE(bb_serialize_json_tok_get_bool(&rec, clean_tok, &v));
    TEST_ASSERT_FALSE(v);
}
