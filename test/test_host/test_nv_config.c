#include "unity.h"
#include "nv_config.h"
#include <string.h>

// config.h defines are available via nv_config's PRIV_INCLUDE_DIRS
// but for native tests we just verify the fallback behavior

void test_nv_config_init(void)
{
    TEST_ASSERT_EQUAL(0, nv_config_init());
}

void test_nv_config_all_empty_before_provisioning(void)
{
    nv_config_init();
    TEST_ASSERT_EQUAL_STRING("", nv_config_wifi_ssid());
    TEST_ASSERT_EQUAL_STRING("", nv_config_wifi_pass());
    TEST_ASSERT_EQUAL_STRING("", nv_config_pool_host());
    TEST_ASSERT_EQUAL_UINT16(0, nv_config_pool_port());
    TEST_ASSERT_EQUAL_STRING("", nv_config_wallet_addr());
    TEST_ASSERT_EQUAL_STRING("", nv_config_worker_name());
}

void test_nv_config_not_provisioned_by_default(void)
{
    nv_config_init();
    TEST_ASSERT_FALSE(nv_config_is_provisioned());
}
