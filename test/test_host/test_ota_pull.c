#include "unity.h"
#include "ota_pull.h"
#include <string.h>

void test_ota_pull_parse_version_found(void)
{
    const char *json =
        "{\"tag_name\":\"v1.2.3\",\"assets\":["
        "{\"name\":\"taipanminer-tdongle-s3.bin\","
        "\"browser_download_url\":\"https://github.com/dangernoodle-io/TaipanMiner/releases/download/v1.2.3/taipanminer-tdongle-s3.bin\"}"
        "]}";

    char tag[32], url[256];
    int ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("v1.2.3", tag);
    TEST_ASSERT_EQUAL_STRING(
        "https://github.com/dangernoodle-io/TaipanMiner/releases/download/v1.2.3/taipanminer-tdongle-s3.bin",
        url);
}

void test_ota_pull_parse_no_matching_asset(void)
{
    const char *json =
        "{\"tag_name\":\"v1.2.3\",\"assets\":["
        "{\"name\":\"taipanminer-bitaxe-601.bin\","
        "\"browser_download_url\":\"https://example.com/taipanminer-bitaxe-601.bin\"}"
        "]}";

    char tag[32], url[256];
    int ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(-2, ret);
    TEST_ASSERT_EQUAL_STRING("v1.2.3", tag);
}

void test_ota_pull_parse_empty_assets(void)
{
    const char *json = "{\"tag_name\":\"v1.0.0\",\"assets\":[]}";

    char tag[32], url[256];
    int ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(-2, ret);
}

void test_ota_pull_parse_no_tag(void)
{
    const char *json = "{\"assets\":[]}";

    char tag[32], url[256];
    int ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_ota_pull_parse_invalid_json(void)
{
    char tag[32], url[256];
    int ret = ota_pull_parse_release_json("not json", "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_ota_pull_parse_multiple_assets(void)
{
    const char *json =
        "{\"tag_name\":\"v2.0.0\",\"assets\":["
        "{\"name\":\"taipanminer-tdongle-s3.bin\","
        "\"browser_download_url\":\"https://example.com/tdongle.bin\"},"
        "{\"name\":\"taipanminer-bitaxe-601.bin\","
        "\"browser_download_url\":\"https://example.com/bitaxe.bin\"}"
        "]}";

    char tag[32], url[256];

    // Should find tdongle-s3
    int ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("https://example.com/tdongle.bin", url);

    // Should find bitaxe-601
    ret = ota_pull_parse_release_json(json, "bitaxe-601", tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("https://example.com/bitaxe.bin", url);
}
