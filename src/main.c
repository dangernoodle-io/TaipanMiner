#include <stdio.h>
#include <inttypes.h>
#include "bb_wifi.h"
#include "bb_prov.h"
#include "bb_mdns.h"
#include "bb_http.h"
#include "bb_ntp.h"
#include "bb_system.h"
#include "mining.h"
#include "work.h"
#include "stratum.h"
#include "bb_nv.h"
#include "taipan_config.h"
#include "taipan_web.h"
#include "display.h"
#include "led.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "partition_fixup.h"
#include "bb_log.h"
#include "bb_ota_pull.h"
#include "bb_ota_push.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef ASIC_BM1370
#include "asic.h"
#endif

static const char *TAG = "taipanminer";

// Mining task handles (for suspend/resume during OTA verification)
#ifdef ASIC_BM1370
TaskHandle_t asic_task_handle = NULL;
#else
TaskHandle_t mining_hw_task_handle = NULL;
#endif

static esp_timer_handle_t s_stats_timer = NULL;
static TaskHandle_t s_stats_save_task = NULL;

static void stats_save_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        mining_lifetime_t lt;
        if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            lt = mining_stats.lifetime;
            xSemaphoreGive(mining_stats.mutex);
            mining_stats_save_lifetime(&lt);
        }
    }
}

static void stats_save_timer_cb(void *arg)
{
    (void)arg;
    if (s_stats_save_task) {
        xTaskNotifyGive(s_stats_save_task);
    }
}

static void start_mining(void)
{
    // Create inter-task queues
    work_queue = xQueueCreate(1, sizeof(mining_work_t));
    result_queue = xQueueCreate(16, sizeof(mining_result_t));

    // Initialize mining stats
    mining_stats_init();

    mining_pause_init();

    // Create task for NVS stats save (low priority, blocks on notification)
    xTaskCreate(stats_save_task, "nvs_save", 4096, NULL, 1, &s_stats_save_task);

    // Start periodic stats save timer (10 minutes)
    const esp_timer_create_args_t timer_args = {
        .callback = stats_save_timer_cb,
        .name = "stats_save",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_stats_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_stats_timer, 10ULL * 60 * 1000000));

    // Register WiFi kick callback for zombie-state recovery
    stratum_set_wifi_kick_cb(bb_wifi_force_reassociate);

    // Start stratum task on Core 0
    xTaskCreatePinnedToCore(stratum_task, "stratum", 8192, NULL, 5, NULL, 0);

    // Start miner task (board-specific config from g_miner_config)
    xTaskCreatePinnedToCore(g_miner_config.task_fn, g_miner_config.name,
                            g_miner_config.stack_size, NULL, g_miner_config.priority,
#ifdef ASIC_BM1370
                            &asic_task_handle,
#else
                            &mining_hw_task_handle,
#endif
                            g_miner_config.core);

    bb_log_i(TAG, "all tasks started");
}

static void display_status_task(void *arg)
{
    (void)arg;
    display_status_t status = {0};
    int tick = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));

        if (tick % 100 == 0) {
            if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
#ifdef ASIC_BM1370
                status.hashrate = mining_stats.asic_ema.value;
                status.temp_c = mining_stats.asic_temp_c;
#else
                status.hashrate = mining_stats.hw_ema.value;
                status.temp_c = mining_stats.temp_c;
#endif
                status.shares = mining_stats.session.shares;
                status.rejected = mining_stats.session.rejected;
                status.uptime_us = esp_timer_get_time() - mining_stats.session.start_us;
                xSemaphoreGive(mining_stats.mutex);
            }

            // Populate network diagnostics (lock-free getters)
            int8_t rssi = 0;
            bb_wifi_get_rssi(&rssi);
            status.rssi = rssi;
            bb_wifi_get_ip_str(status.ip, sizeof(status.ip));
            int64_t age_us = 0;
            bb_wifi_get_disconnect(&status.wifi_disc_reason, &age_us);
            status.wifi_disc_age_s = (uint32_t)(age_us / 1000000);
            status.wifi_retry_count = bb_wifi_get_retry_count();
            status.mdns_ok = bb_mdns_started();
            status.stratum_ok = stratum_is_connected();
            status.stratum_reconnect_ms = stratum_get_reconnect_delay_ms();
            status.stratum_fail_count = stratum_get_connect_fail_count();
        }
        tick++;

        display_show_status(&status);
    }
}

static void log_reset_reason(void)
{
    const char *reason_str = bb_system_reset_reason_str(bb_system_get_reset_reason());
    bb_log_i(TAG, "reset reason: %s", reason_str);

    if (bb_system_is_abnormal_reset()) {
        bb_log_w(TAG, "abnormal reset detected (%s)", reason_str);
        uint32_t wdt_count = 0;
        bb_nv_get_u32("taipanminer", "wdt_resets", &wdt_count, 0);
        wdt_count++;
        if (bb_nv_set_u32("taipanminer", "wdt_resets", wdt_count) == BB_OK) {
            bb_log_w(TAG, "abnormal reset count: %" PRIu32, wdt_count);
        }
    }
}

// cppcheck-suppress unusedFunction
void app_main(void)
{
    partition_fixup_check();

    const esp_app_desc_t *app = esp_app_get_description();
    bb_log_i(TAG, "%s v%s (%s %s, IDF %s) starting...",
             app->project_name, app->version, app->date, app->time, app->idf_ver);

    // Suppress noisy framework log tags (before wifi_init)
    bb_log_level_set("wifi", BB_LOG_LEVEL_WARN);
    bb_log_level_set("wifi_init", BB_LOG_LEVEL_WARN);
    bb_log_level_set("net80211", BB_LOG_LEVEL_WARN);
    bb_log_level_set("pp", BB_LOG_LEVEL_WARN);
    bb_log_level_set("phy_init", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp_netif_handlers", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp_netif_lwip", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp-x509-crt-bundle", BB_LOG_LEVEL_WARN);

    // Register TM-owned tags so they appear in GET /api/log/level discovery
    bb_log_tag_register("taipanminer", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("stratum", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("mining", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("sha256_hw", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("display", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("led", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("ota_validator", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("partition_fixup", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("taipan_config", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("web", BB_LOG_LEVEL_INFO);
#ifdef ASIC_BM1370
    bb_log_tag_register("asic", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("bm1370", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("emc2101", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("tps546", BB_LOG_LEVEL_INFO);
#else
    bb_log_tag_register("bm1368", BB_LOG_LEVEL_INFO);
#endif

    ESP_ERROR_CHECK(bb_log_stream_init());

    // Initialize NVS (required by WiFi)
    ESP_ERROR_CHECK(bb_nv_flash_init());

    // Load config from NVS (falls back to defaults)
    ESP_ERROR_CHECK(bb_nv_config_init());
    ESP_ERROR_CHECK(taipan_config_init());
    log_reset_reason();
    ESP_ERROR_CHECK(led_init());

    // Boot failure counter — incremented only on WiFi timeout restart (wifi_prov.c),
    // not on every boot, so flash/power-cycle doesn't trigger AP fallback.
    uint8_t boot_cnt = bb_nv_config_boot_count();

    // Register TaipanMiner-specific info extender for breadboard's /api/info endpoint
    ESP_ERROR_CHECK(taipan_web_register_info_extender());

    if (boot_cnt >= BB_NV_CONFIG_BOOT_FAIL_THRESHOLD && bb_nv_config_is_provisioned()) {
        bb_log_w(TAG, "boot_count=%" PRIu8 " >= %d: clearing provisioning for AP fallback",
                 boot_cnt, BB_NV_CONFIG_BOOT_FAIL_THRESHOLD);
        bb_nv_config_clear_wifi();
        bb_nv_config_clear_provisioned();
        bb_nv_config_reset_boot_count();
    } else if (boot_cnt > 1) {
        bb_log_w(TAG, "boot_count=%" PRIu8 " (%d until AP fallback)",
                 boot_cnt, BB_NV_CONFIG_BOOT_FAIL_THRESHOLD - boot_cnt);
    }

#ifdef ASIC_BM1370
    // Initialize ASIC before WiFi — freq ramp takes ~8s, runs while WiFi isn't needed yet.
    // Skip if not provisioned (will enter AP mode instead).
    if (bb_nv_config_is_provisioned()) {
        ESP_ERROR_CHECK(asic_init());
    }
#endif

    // Initialize display early so splash is visible during boot.
    // On Bitaxe, display creates I2C bus itself if asic_init() hasn't run.
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(display_show_splash());
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Set CORS methods before HTTP server starts (required for PATCH support)
    bb_http_set_cors_methods("GET, POST, PATCH, OPTIONS");

    if (!bb_nv_config_is_provisioned()) {
        bb_log_i(TAG, "entering provisioning mode");
        // Configure provisioning before AP start
        bb_prov_set_ap_ssid_prefix("TaipanMiner-");
        bb_prov_set_ap_password("taipanminer");
        bb_mdns_set_service_type("_taipanminer");
        bb_mdns_set_instance_name("TaipanMiner");
        {
            char hn[64];
            bb_mdns_build_hostname("taipanminer", taipan_config_worker_name(), hn, sizeof(hn));
            bb_mdns_set_hostname(hn);
        }

        ESP_ERROR_CHECK(bb_prov_start_ap());
        taipan_web_install_prov_save_cb();
        {
            size_t n;
            const bb_http_asset_t *assets = taipan_web_prov_assets(&n);
            ESP_ERROR_CHECK(bb_prov_start(assets, n, NULL));
        }

        // Show provisioning info on display + solid blue LED
        char ap_ssid[32];
        bb_prov_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
        ESP_ERROR_CHECK(display_show_prov(ap_ssid, "taipanminer"));
        ESP_ERROR_CHECK(led_set_color(0, 0, 38));

        bool connected = false;
        while (!connected) {
            bb_prov_wait_done(UINT32_MAX);

            bb_prov_stop_ap();

            esp_err_t err = bb_wifi_init_sta();
            if (err == ESP_OK) {
                ESP_ERROR_CHECK(led_off());
                bb_log_i(TAG, "provisioning complete");
                bb_nv_config_set_provisioned();
                bb_nv_config_reset_boot_count();
                connected = true;
                // Stop provisioning and switch to mining mode
                bb_prov_stop();
                ESP_ERROR_CHECK(taipan_web_register_mining_routes(bb_http_server_get_handle()));
            } else {
                bb_log_w(TAG, "STA connect failed, re-entering provisioning");
                // Reinitialize AP for retry
                ESP_ERROR_CHECK(bb_prov_start_ap());
            }
        }
    } else {
        // Normal boot: connect to saved WiFi
        bb_mdns_set_service_type("_taipanminer");
        bb_mdns_set_instance_name("TaipanMiner");
        {
            char hn[64];
            bb_mdns_build_hostname("taipanminer", taipan_config_worker_name(), hn, sizeof(hn));
            bb_mdns_set_hostname(hn);
        }
        bb_mdns_init();
        ESP_ERROR_CHECK(bb_wifi_init());
        ESP_ERROR_CHECK(bb_http_server_ensure_started());
        ESP_ERROR_CHECK(taipan_web_register_mining_routes(bb_http_server_get_handle()));
    }

    // Sync time via SNTP (UTC)
    bb_ntp_start("pool.ntp.org");

    // Initialize OTA pull with breadboard component
    bb_ota_pull_set_releases_url("https://api.github.com/repos/dangernoodle-io/TaipanMiner/releases/latest");
    bb_ota_pull_set_firmware_board("taipanminer-" FIRMWARE_BOARD);
    bb_ota_pull_set_hooks(mining_pause, mining_resume);
    bb_ota_pull_set_skip_check_cb(bb_nv_config_ota_skip_check);

    // Initialize OTA push with breadboard component
    bb_ota_push_set_hooks(mining_pause, mining_resume);
    bb_ota_push_set_skip_check_cb(bb_nv_config_ota_skip_check);

    // Start mining
    start_mining();

    // Start display status task on Core 0
    xTaskCreatePinnedToCore(display_status_task, "display", 4096, NULL, 2, NULL, 0);
}
