#include "unity.h"
#include "bb_json.h"
#include "bb_manifest.h"
#include "taipan_config.h"
#include <string.h>

void test_register_manifest_returns_ok(void)
{
    bb_manifest_clear();
    TEST_ASSERT_EQUAL(0, taipan_config_register_manifest());
}

void test_register_manifest_registers_six_keys(void)
{
    bb_manifest_clear();
    TEST_ASSERT_EQUAL(0, taipan_config_register_manifest());
    bb_json_t doc = bb_manifest_emit();
    TEST_ASSERT_NOT_NULL(doc);
    char *json = bb_json_serialize(doc);
    TEST_ASSERT_NOT_NULL(json);
    const char *keys[] = {"pool_host", "pool_port", "wallet_addr", "worker", "pool_pass", "hostname"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, keys[i]), keys[i]);
    }
    bb_json_free_str(json);
    bb_json_free(doc);
}

void test_register_manifest_idempotent_via_clear(void)
{
    bb_manifest_clear();
    TEST_ASSERT_EQUAL(0, taipan_config_register_manifest());
    bb_manifest_clear();
    TEST_ASSERT_EQUAL(0, taipan_config_register_manifest());
}
