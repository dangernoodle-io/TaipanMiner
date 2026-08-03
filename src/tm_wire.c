// TM-local composition-root helpers -- see tm_wire.h for why these are
// wrapped as bb_err_t-returning, zero-arg functions instead of calling
// bb_log_level_set()/bb_log_tag_register()/reset-reason logging inline from
// bb_app_init.c's generated `// bbtool:init` call sequence.
#include "tm_wire.h"
#include "bb_log.h"
#include "bb_system.h"
#include "mining.h"

static const char *TAG = "taipanminer";

bb_err_t tm_log_noise_suppress_init(void)
{
    // Framework log-noise tags -- meaningful once WiFi bring-up runs.
    // codegen's early-tier tie-break sorts consumer-manifest entries after
    // every component-header entry in the same tier (parse order), so this
    // now runs after bb_wifi_autoinit() rather than before it (previously
    // hand-ordered first) -- a cosmetic log-verbosity window during the
    // first connect attempt, not a correctness change.
    bb_log_level_set("wifi", BB_LOG_LEVEL_WARN);
    bb_log_level_set("wifi_init", BB_LOG_LEVEL_WARN);
    bb_log_level_set("net80211", BB_LOG_LEVEL_WARN);
    bb_log_level_set("pp", BB_LOG_LEVEL_WARN);
    bb_log_level_set("phy_init", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp_netif_handlers", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp_netif_lwip", BB_LOG_LEVEL_WARN);

    bb_log_tag_register("taipanminer", BB_LOG_LEVEL_INFO);
    return BB_OK;
}

bb_err_t tm_log_reset_reason_init(void)
{
    const char *reason_str = bb_system_reset_reason_str(bb_system_get_reset_reason());
    bb_log_i(TAG, "reset reason: %s", reason_str);

    if (bb_system_is_abnormal_reset()) {
        bb_log_w(TAG, "abnormal reset detected (%s)", reason_str);
    }
    return BB_OK;
}

bb_err_t tm_mining_self_test_init(void)
{
    mining_run_self_tests();
    return BB_OK;
}
