#include "unity.h"
#include "nv_config.h"
#include "taipan_config.h"
#include <string.h>

void test_nv_config_init(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, taipan_config_init());
}

void test_nv_config_all_empty_before_provisioning(void)
{
    bb_nv_config_init();
    taipan_config_init();
    TEST_ASSERT_EQUAL_STRING("", bb_nv_config_wifi_ssid());
    TEST_ASSERT_EQUAL_STRING("", bb_nv_config_wifi_pass());
    TEST_ASSERT_EQUAL_STRING("", taipan_config_pool_host());
    TEST_ASSERT_EQUAL_UINT16(0, taipan_config_pool_port());
    TEST_ASSERT_EQUAL_STRING("", taipan_config_wallet_addr());
    TEST_ASSERT_EQUAL_STRING("", taipan_config_worker_name());
    TEST_ASSERT_EQUAL_STRING("", taipan_config_pool_pass());
}

void test_nv_config_not_provisioned_by_default(void)
{
    bb_nv_config_init();
    TEST_ASSERT_FALSE(bb_nv_config_is_provisioned());
}
