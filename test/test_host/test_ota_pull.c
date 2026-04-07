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

void test_ota_pull_parse_tag_truncation(void)
{
    const char *json =
        "{\"tag_name\":\"v99.99.99-rc1-longprerelease\",\"assets\":["
        "{\"name\":\"taipanminer-tdongle-s3.bin\","
        "\"browser_download_url\":\"https://example.com/test.bin\"}"
        "]}";

    char tag[8], url[256];
    int ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(0, ret);
    // Tag should be truncated to 7 chars + null terminator
    TEST_ASSERT_EQUAL_INT(7, (int)strlen(tag));
    TEST_ASSERT_EQUAL_STRING("v99.99.", tag);
    // Null terminator should be properly set
    TEST_ASSERT_EQUAL_CHAR('\0', tag[7]);
}

void test_ota_pull_parse_url_truncation(void)
{
    const char *json =
        "{\"tag_name\":\"v1.0.0\",\"assets\":["
        "{\"name\":\"taipanminer-tdongle-s3.bin\","
        "\"browser_download_url\":\"https://example.com/very/long/path/to/firmware/image.bin\"}"
        "]}";

    char tag[32], url[32];
    int ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(0, ret);
    // URL should be truncated to 31 chars + null terminator
    TEST_ASSERT_EQUAL_INT(31, (int)strlen(url));
    // Null terminator should be properly set
    TEST_ASSERT_EQUAL_CHAR('\0', url[31]);
    // Verify it's truncated from the beginning of the URL
    TEST_ASSERT_EQUAL_INT(0, strncmp(url, "https://example.com/very/long", 29));
}

void test_ota_pull_parse_asset_missing_url(void)
{
    const char *json =
        "{\"tag_name\":\"v1.0.0\",\"assets\":["
        "{\"name\":\"taipanminer-tdongle-s3.bin\","
        "\"size\":1234}"
        "]}";

    char tag[32], url[256];
    int ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    // Should return -2 (no matching asset with valid URL)
    TEST_ASSERT_EQUAL_INT(-2, ret);
    // Tag should still be populated from tag_name
    TEST_ASSERT_EQUAL_STRING("v1.0.0", tag);
}

void test_ota_pull_parse_assets_not_array(void)
{
    const char *json =
        "{\"tag_name\":\"v1.0.0\","
        "\"assets\":\"not-an-array\"}";

    char tag[32], url[256];
    int ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    // Should return -2 because assets is not an array
    TEST_ASSERT_EQUAL_INT(-2, ret);
}

void test_ota_pull_parse_null_inputs(void)
{
    const char *json =
        "{\"tag_name\":\"v1.0.0\",\"assets\":["
        "{\"name\":\"taipanminer-tdongle-s3.bin\","
        "\"browser_download_url\":\"https://example.com/test.bin\"}"
        "]}";

    char tag[32], url[256];

    // NULL json
    int ret = ota_pull_parse_release_json(NULL, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(-1, ret);

    // NULL board_name
    ret = ota_pull_parse_release_json(json, NULL, tag, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(-1, ret);

    // NULL out_tag
    ret = ota_pull_parse_release_json(json, "tdongle-s3", NULL, sizeof(tag), url, sizeof(url));
    TEST_ASSERT_EQUAL_INT(-1, ret);

    // NULL out_url
    ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), NULL, sizeof(url));
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_ota_pull_parse_asset_url_null_value(void)
{
    const char *json =
        "{\"tag_name\":\"v1.0.0\",\"assets\":["
        "{\"name\":\"taipanminer-tdongle-s3.bin\","
        "\"browser_download_url\":null}"
        "]}";

    char tag[32], url[256];
    int ret = ota_pull_parse_release_json(json, "tdongle-s3", tag, sizeof(tag), url, sizeof(url));
    // Should return -2 because url_item->valuestring is NULL
    TEST_ASSERT_EQUAL_INT(-2, ret);
}
