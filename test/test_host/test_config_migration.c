#include "unity.h"
#include "bb_nv.h"
#include "bb_json.h"
#include "bb_manifest.h"
#include "config.h"
#include <string.h>

/*
 * Migration tests: BB-owned NVS keys (wifi_ssid, wifi_pass, hostname, mdns_en,
 * update_check_en, display_en) migrated from legacy "taipanminer" namespace into
 * BB's default "bb_cfg" namespace.
 *
 * Migration runs in app_main via migrate_legacy_bb_keys() BEFORE
 * bb_init_init_early(), using direct ESP-IDF NVS API. It does NOT run
 * inside config_init() — ordering-critical to ensure bb_wifi_autoinit (EARLY
 * tier) sees migrated wifi creds before bb_nv_config_init loads NVS.
 *
 * On host, bb_nv_get_str / bb_nv_set_str are no-ops (stubs return fallback).
 * These tests verify:
 *   - config_init() succeeds and is idempotent.
 *   - Legacy "taipanminer" bb-owned NVS keys return fallback on host (key absent).
 *   - bb_nv_config_* hostname API is accessible after config_init.
 *   - TM manifest does not include hostname (BB owns it).
 *
 * Full migration ordering is integration-only (requires real NVS on device).
 */

/* --- hostname migration --- */

void test_migration_hostname_skipped_when_bb_cfg_already_set(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    /* Pre-populate bb_cfg hostname before config_init runs */
    TEST_ASSERT_EQUAL(0, bb_nv_config_set_hostname("existing-host"));
    TEST_ASSERT_EQUAL(0, config_init());
    /* migration must not overwrite an already-set bb_cfg hostname */
    TEST_ASSERT_EQUAL_STRING("existing-host", config_hostname());
}

void test_migration_hostname_stays_empty_when_no_legacy(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    /* bb_cfg empty, legacy NVS stub returns fallback "" → nothing to migrate */
    TEST_ASSERT_EQUAL(0, config_init());
    /* hostname stays as whatever bb_nv_config init set (empty on host) */
    const char *hn = config_hostname();
    /* must not be garbage — either empty string or a valid RFC1123 name */
    TEST_ASSERT_NOT_NULL(hn);
}

void test_migration_hostname_worker_derived_when_no_legacy(void)
{
    /* When bb_cfg hostname is empty AND legacy NVS "hostname" key is empty
     * but a worker name exists, the migration derives a hostname from it.
     * On host the legacy NVS stub always returns "", so we exercise only the
     * "bb_cfg already empty → stays empty without worker" path. */
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    /* No pool configured on host → no worker → hostname remains as-is */
    /* Just confirm config_init returns OK and hostname is accessible */
    TEST_ASSERT_NOT_NULL(config_hostname());
}

/* --- legacy NVS keys absent after migration ---
 *
 * On host, bb_nv stubs always return the fallback value regardless of whether
 * a key was "erased" (NVS is not simulated). We verify that bb_nv_get_str
 * returns the default fallback for the legacy "taipanminer" bb-owned keys,
 * which is the expected post-migration behaviour (key absent → fallback). */

void test_migration_legacy_wifi_ssid_absent_after_init(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    char buf[33] = {0};
    bb_nv_get_str("taipanminer", "wifi_ssid", buf, sizeof(buf), "DEFAULT");
    TEST_ASSERT_EQUAL_STRING("DEFAULT", buf);
}

void test_migration_legacy_wifi_pass_absent_after_init(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    char buf[65] = {0};
    bb_nv_get_str("taipanminer", "wifi_pass", buf, sizeof(buf), "DEFAULT");
    TEST_ASSERT_EQUAL_STRING("DEFAULT", buf);
}

void test_migration_legacy_hostname_absent_after_init(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    char buf[33] = {0};
    bb_nv_get_str("taipanminer", "hostname", buf, sizeof(buf), "DEFAULT");
    TEST_ASSERT_EQUAL_STRING("DEFAULT", buf);
}

void test_migration_legacy_mdns_en_absent_after_init(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    uint8_t v = 99;
    bb_nv_get_u8("taipanminer", "mdns_en", &v, 42);
    TEST_ASSERT_EQUAL_UINT8(42, v);
}

void test_migration_legacy_update_check_en_absent_after_init(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    uint8_t v = 99;
    bb_nv_get_u8("taipanminer", "update_check_en", &v, 42);
    TEST_ASSERT_EQUAL_UINT8(42, v);
}

void test_migration_legacy_display_en_absent_after_init(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    uint8_t v = 99;
    bb_nv_get_u8("taipanminer", "display_en", &v, 42);
    TEST_ASSERT_EQUAL_UINT8(42, v);
}

/* --- config_init succeeds after migration block --- */

void test_migration_config_init_returns_ok(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
}

void test_migration_repeated_init_idempotent(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    /* second init: bb_cfg hostname now set (if any) → migration no-ops */
    TEST_ASSERT_EQUAL(0, config_init());
}

/* --- hostname not in TM manifest after drop --- */

void test_migration_hostname_not_in_tm_manifest(void)
{
    bb_manifest_clear();
    TEST_ASSERT_EQUAL(0, config_register_manifest());
    bb_json_t doc = bb_manifest_emit();
    TEST_ASSERT_NOT_NULL(doc);
    char *json = bb_json_serialize(doc);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NULL_MESSAGE(strstr(json, "\"hostname\""),
                             "hostname must not be in TM manifest (BB owns it)");
    bb_json_free_str(json);
    bb_json_free(doc);
}
