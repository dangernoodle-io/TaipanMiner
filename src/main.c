// TaipanMiner v2 — Phase 1 skeleton entry point.
//
// Headless bb_init host shell: bring WiFi/HTTP/mDNS/NVS up via breadboard's
// registry tiers, log boot identity, then idle. No ASIC/mining/stratum here —
// those land as tm_* domain components in later sub-PRs against jae/tm-v2.
//
// Bringup sequence mirrors breadboard's own examples/smoke entry shim:
//   1. bb_init_init_early() — EARLY tier (bb_nv_config_init, bb_wifi_init_sta).
//   2. bb_init_init()       — PRE_HTTP tier, then HTTP autostart, then the
//                             regular route-registration tier (bb_info,
//                             bb_board, bb_manifest, bb_openapi, bb_mdns).
#include "bb_log.h"
#include "bb_init.h"
#include "bb_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "taipanminer";

void app_main(void)
{
    bb_log_i(TAG, "%s v%s (%s %s, IDF %s) starting...",
             bb_system_get_project_name(), bb_system_get_version(),
             bb_system_get_build_date(), bb_system_get_build_time(),
             bb_system_get_idf_version());

    // Suppress noisy framework log tags (before wifi_init).
    bb_log_level_set("wifi", BB_LOG_LEVEL_WARN);
    bb_log_level_set("wifi_init", BB_LOG_LEVEL_WARN);
    bb_log_level_set("net80211", BB_LOG_LEVEL_WARN);
    bb_log_level_set("pp", BB_LOG_LEVEL_WARN);
    bb_log_level_set("phy_init", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp_netif_handlers", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp_netif_lwip", BB_LOG_LEVEL_WARN);
    bb_log_level_set("httpd_uri", BB_LOG_LEVEL_WARN);
    bb_log_level_set("httpd_txrx", BB_LOG_LEVEL_WARN);
    bb_log_level_set("httpd", BB_LOG_LEVEL_WARN);

    bb_log_tag_register("taipanminer", BB_LOG_LEVEL_INFO);

    BB_ERROR_CHECK(bb_init_init_early());
    BB_ERROR_CHECK(bb_init_init());

    bb_log_i(TAG, "boot ok, idle");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
