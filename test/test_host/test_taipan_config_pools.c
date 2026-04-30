#include "unity.h"
#include "taipan_config.h"
#include <string.h>

static taipan_pool_cfg_t make_pool(const char *host, uint16_t port,
                                   const char *wallet, const char *worker,
                                   const char *pass)
{
    taipan_pool_cfg_t p = {0};
    strncpy(p.host,   host,   sizeof(p.host)   - 1);
    p.port = port;
    strncpy(p.wallet, wallet, sizeof(p.wallet) - 1);
    strncpy(p.worker, worker, sizeof(p.worker) - 1);
    strncpy(p.pass,   pass,   sizeof(p.pass)   - 1);
    /* Defaults match firmware NVS load defaults: TA-306 off, TA-307 on. */
    p.extranonce_subscribe = false;
    p.decode_coinbase      = true;
    return p;
}

void test_set_pools_null_primary_rejected(void)
{
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, taipan_config_set_pools(NULL, NULL));
}

void test_set_pools_primary_only_clears_fallback(void)
{
    // First seed both slots, then set_pools with NULL fallback to clear it.
    taipan_pool_cfg_t primary  = make_pool("p.example.com", 3333, "bc1qprimary",  "miner-1", "x");
    taipan_pool_cfg_t fallback = make_pool("f.example.com", 3334, "bc1qfallback", "miner-2", "y");
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, &fallback));
    TEST_ASSERT_TRUE(taipan_config_pool_configured(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_TRUE(taipan_config_pool_configured(TAIPAN_POOL_FALLBACK));

    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, NULL));
    TEST_ASSERT_TRUE(taipan_config_pool_configured(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_FALSE(taipan_config_pool_configured(TAIPAN_POOL_FALLBACK));
    TEST_ASSERT_EQUAL_STRING("", taipan_config_pool_host_idx(TAIPAN_POOL_FALLBACK));
    TEST_ASSERT_EQUAL_UINT16(0, taipan_config_pool_port_idx(TAIPAN_POOL_FALLBACK));
}

void test_set_pools_writes_both_slots(void)
{
    taipan_pool_cfg_t primary  = make_pool("primary.example.com", 3333,
                                           "bc1qprimary", "miner-1", "x");
    taipan_pool_cfg_t fallback = make_pool("fallback.example.com", 3334,
                                           "bc1qfallback", "miner-2", "y");
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, &fallback));

    TEST_ASSERT_EQUAL_STRING("primary.example.com",
                             taipan_config_pool_host_idx(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_EQUAL_UINT16(3333, taipan_config_pool_port_idx(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_EQUAL_STRING("bc1qprimary",
                             taipan_config_wallet_addr_idx(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_EQUAL_STRING("miner-1",
                             taipan_config_worker_name_idx(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_EQUAL_STRING("x",
                             taipan_config_pool_pass_idx(TAIPAN_POOL_PRIMARY));

    TEST_ASSERT_EQUAL_STRING("fallback.example.com",
                             taipan_config_pool_host_idx(TAIPAN_POOL_FALLBACK));
    TEST_ASSERT_EQUAL_UINT16(3334, taipan_config_pool_port_idx(TAIPAN_POOL_FALLBACK));
    TEST_ASSERT_EQUAL_STRING("bc1qfallback",
                             taipan_config_wallet_addr_idx(TAIPAN_POOL_FALLBACK));
    TEST_ASSERT_EQUAL_STRING("miner-2",
                             taipan_config_worker_name_idx(TAIPAN_POOL_FALLBACK));
    TEST_ASSERT_EQUAL_STRING("y",
                             taipan_config_pool_pass_idx(TAIPAN_POOL_FALLBACK));
}

void test_pool_configured_requires_all_fields(void)
{
    // Missing port → not configured.
    taipan_pool_cfg_t primary = make_pool("a.example.com", 0, "bc1qa", "w-1", "x");
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, NULL));
    TEST_ASSERT_FALSE(taipan_config_pool_configured(TAIPAN_POOL_PRIMARY));

    // Missing wallet → not configured.
    primary = make_pool("a.example.com", 3333, "", "w-1", "x");
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, NULL));
    TEST_ASSERT_FALSE(taipan_config_pool_configured(TAIPAN_POOL_PRIMARY));

    // Missing worker → not configured.
    primary = make_pool("a.example.com", 3333, "bc1qa", "", "x");
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, NULL));
    TEST_ASSERT_FALSE(taipan_config_pool_configured(TAIPAN_POOL_PRIMARY));

    // Missing host → not configured.
    primary = make_pool("", 3333, "bc1qa", "w-1", "x");
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, NULL));
    TEST_ASSERT_FALSE(taipan_config_pool_configured(TAIPAN_POOL_PRIMARY));

    // All four set → configured. (Empty pass is OK — many pools accept any.)
    primary = make_pool("a.example.com", 3333, "bc1qa", "w-1", "");
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, NULL));
    TEST_ASSERT_TRUE(taipan_config_pool_configured(TAIPAN_POOL_PRIMARY));
}

void test_pool_configured_rejects_out_of_range_idx(void)
{
    TEST_ASSERT_FALSE(taipan_config_pool_configured(-1));
    TEST_ASSERT_FALSE(taipan_config_pool_configured(TAIPAN_POOL_COUNT));
    TEST_ASSERT_FALSE(taipan_config_pool_configured(99));
}

void test_pool_idx_accessors_out_of_range_safe(void)
{
    TEST_ASSERT_EQUAL_STRING("", taipan_config_pool_host_idx(-1));
    TEST_ASSERT_EQUAL_UINT16(0, taipan_config_pool_port_idx(-1));
    TEST_ASSERT_EQUAL_STRING("", taipan_config_wallet_addr_idx(-1));
    TEST_ASSERT_EQUAL_STRING("", taipan_config_worker_name_idx(-1));
    TEST_ASSERT_EQUAL_STRING("", taipan_config_pool_pass_idx(-1));

    TEST_ASSERT_EQUAL_STRING("", taipan_config_pool_host_idx(TAIPAN_POOL_COUNT));
    TEST_ASSERT_EQUAL_UINT16(0, taipan_config_pool_port_idx(TAIPAN_POOL_COUNT));
}

void test_legacy_primary_accessors_alias_idx_zero(void)
{
    taipan_pool_cfg_t primary = make_pool("legacy.example.com", 1234,
                                          "bc1qlegacy", "legacy-w", "lp");
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, NULL));
    TEST_ASSERT_EQUAL_STRING("legacy.example.com", taipan_config_pool_host());
    TEST_ASSERT_EQUAL_UINT16(1234, taipan_config_pool_port());
    TEST_ASSERT_EQUAL_STRING("bc1qlegacy", taipan_config_wallet_addr());
    TEST_ASSERT_EQUAL_STRING("legacy-w", taipan_config_worker_name());
    TEST_ASSERT_EQUAL_STRING("lp", taipan_config_pool_pass());
}

void test_legacy_set_pool_preserves_fallback(void)
{
    // Seed a fallback first.
    taipan_pool_cfg_t primary  = make_pool("p.example.com", 3333, "bc1qa", "w-a", "x");
    taipan_pool_cfg_t fallback = make_pool("f.example.com", 3334, "bc1qb", "w-b", "y");
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, &fallback));

    // Legacy single-slot setter should round-trip into the primary slot
    // while preserving the fallback (read from the in-memory cache).
    TEST_ASSERT_EQUAL(BB_OK,
        taipan_config_set_pool("new-primary.example.com", 9999,
                               "bc1qnew", "new-worker", "np"));
    TEST_ASSERT_EQUAL_STRING("new-primary.example.com",
                             taipan_config_pool_host_idx(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_EQUAL_UINT16(9999, taipan_config_pool_port_idx(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_EQUAL_STRING("bc1qnew",
                             taipan_config_wallet_addr_idx(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_EQUAL_STRING("f.example.com",
                             taipan_config_pool_host_idx(TAIPAN_POOL_FALLBACK));
    TEST_ASSERT_TRUE(taipan_config_pool_configured(TAIPAN_POOL_FALLBACK));
}

void test_legacy_set_pool_no_fallback_leaves_slot_clear(void)
{
    // Start with no fallback; legacy setter should not invent one.
    taipan_pool_cfg_t primary = make_pool("solo.example.com", 1111, "bc1qsolo", "solo", "x");
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, NULL));
    TEST_ASSERT_FALSE(taipan_config_pool_configured(TAIPAN_POOL_FALLBACK));

    TEST_ASSERT_EQUAL(BB_OK,
        taipan_config_set_pool("solo2.example.com", 2222, "bc1qsolo2", "solo2", "y"));
    TEST_ASSERT_FALSE(taipan_config_pool_configured(TAIPAN_POOL_FALLBACK));
    TEST_ASSERT_EQUAL_STRING("solo2.example.com",
                             taipan_config_pool_host_idx(TAIPAN_POOL_PRIMARY));
}

void test_set_pools_truncates_oversized_fields(void)
{
    // host buffer is 64 bytes — fill with 80 chars to confirm truncation
    // doesn't overflow and the result is null-terminated.
    char long_host[80];
    memset(long_host, 'h', sizeof(long_host) - 1);
    long_host[sizeof(long_host) - 1] = '\0';

    taipan_pool_cfg_t primary = {0};
    strncpy(primary.host, long_host, sizeof(primary.host) - 1);
    primary.port = 3333;
    strncpy(primary.wallet, "bc1qw", sizeof(primary.wallet) - 1);
    strncpy(primary.worker, "w", sizeof(primary.worker) - 1);

    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, NULL));
    const char *got = taipan_config_pool_host_idx(TAIPAN_POOL_PRIMARY);
    TEST_ASSERT_TRUE(strlen(got) < sizeof(primary.host));
    TEST_ASSERT_EQUAL('h', got[0]);
}

void test_pool_options_round_trip(void)
{
    /* TA-306 / TA-307 per-slot bool fields: round-trip through set_pools
     * and expose via the *_idx accessors. */
    taipan_pool_cfg_t primary  = make_pool("p.example.com", 3333, "bc1qa", "w-a", "x");
    taipan_pool_cfg_t fallback = make_pool("f.example.com", 3334, "bc1qb", "w-b", "y");
    primary.extranonce_subscribe  = true;
    primary.decode_coinbase       = false;
    fallback.extranonce_subscribe = false;
    fallback.decode_coinbase      = true;

    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, &fallback));
    TEST_ASSERT_TRUE(taipan_config_pool_extranonce_subscribe_idx(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_FALSE(taipan_config_pool_decode_coinbase_idx(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_FALSE(taipan_config_pool_extranonce_subscribe_idx(TAIPAN_POOL_FALLBACK));
    TEST_ASSERT_TRUE(taipan_config_pool_decode_coinbase_idx(TAIPAN_POOL_FALLBACK));
}

void test_pool_options_legacy_set_pool_preserves_flags(void)
{
    /* The legacy single-slot setter doesn't accept the new bool fields, so
     * it must read-back the current values rather than reset them. */
    taipan_pool_cfg_t primary = make_pool("p.example.com", 3333, "bc1qa", "w-a", "x");
    primary.extranonce_subscribe = true;
    primary.decode_coinbase      = false;
    TEST_ASSERT_EQUAL(BB_OK, taipan_config_set_pools(&primary, NULL));

    TEST_ASSERT_EQUAL(BB_OK,
        taipan_config_set_pool("new.example.com", 9999, "bc1qnew", "new-w", "np"));
    TEST_ASSERT_TRUE(taipan_config_pool_extranonce_subscribe_idx(TAIPAN_POOL_PRIMARY));
    TEST_ASSERT_FALSE(taipan_config_pool_decode_coinbase_idx(TAIPAN_POOL_PRIMARY));
}

void test_pool_options_idx_out_of_range_safe(void)
{
    /* Bounds-safe defaults: extranonce false, decode true. */
    TEST_ASSERT_FALSE(taipan_config_pool_extranonce_subscribe_idx(-1));
    TEST_ASSERT_FALSE(taipan_config_pool_extranonce_subscribe_idx(TAIPAN_POOL_COUNT));
    TEST_ASSERT_TRUE(taipan_config_pool_decode_coinbase_idx(-1));
    TEST_ASSERT_TRUE(taipan_config_pool_decode_coinbase_idx(TAIPAN_POOL_COUNT));
}
