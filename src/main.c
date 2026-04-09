#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "nvs.h"
#include "esp_sntp.h"
#include "board.h"
#include "wifi_prov.h"
#include "mining.h"
#include "work.h"
#include "stratum.h"
#include "nv_config.h"
#include "http_server.h"
#include "display.h"
#include "led.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "partition_fixup.h"
#include "log_stream.h"
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

static void sntp_init_time(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

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
    // Confirm OTA image if pending verification (rollback protection)
    esp_ota_img_states_t ota_state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "OTA: firmware validated");

            // Reset WDT counter on new firmware — clean baseline per release
            nvs_handle_t h;
            if (nvs_open("taipanminer", NVS_READWRITE, &h) == ESP_OK) {
                nvs_erase_key(h, "wdt_resets");
                nvs_commit(h);
                nvs_close(h);
            }
        }
    }

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

    ESP_LOGI(TAG, "all tasks started");
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
                status.hashrate = mining_stats.hw_ema.value;
                status.temp_c = mining_stats.temp_c;
                status.shares = mining_stats.session.shares;
                status.rejected = mining_stats.session.rejected;
                status.uptime_us = esp_timer_get_time() - mining_stats.session.start_us;
#ifdef ASIC_BM1370
                status.hashrate = mining_stats.asic_ema.value;
                status.temp_c = mining_stats.asic_temp_c;
#endif
                xSemaphoreGive(mining_stats.mutex);
            }
        }
        tick++;

        display_show_status(&status);
    }
}

static void log_reset_reason(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    const char *reason_str;
    switch (reason) {
    case ESP_RST_POWERON:   reason_str = "power-on"; break;
    case ESP_RST_SW:        reason_str = "software"; break;
    case ESP_RST_PANIC:     reason_str = "panic"; break;
    case ESP_RST_TASK_WDT:  reason_str = "task_wdt"; break;
    case ESP_RST_WDT:       reason_str = "wdt"; break;
    case ESP_RST_DEEPSLEEP: reason_str = "deep_sleep"; break;
    case ESP_RST_BROWNOUT:  reason_str = "brownout"; break;
    default:                reason_str = "unknown"; break;
    }

    ESP_LOGI(TAG, "reset reason: %s", reason_str);

    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT || reason == ESP_RST_PANIC) {
        ESP_LOGW(TAG, "abnormal reset detected (%s)", reason_str);
        nvs_handle_t h;
        if (nvs_open("taipanminer", NVS_READWRITE, &h) == ESP_OK) {
            uint32_t wdt_count = 0;
            nvs_get_u32(h, "wdt_resets", &wdt_count);
            wdt_count++;
            nvs_set_u32(h, "wdt_resets", wdt_count);
            nvs_commit(h);
            nvs_close(h);
            ESP_LOGW(TAG, "abnormal reset count: %" PRIu32, wdt_count);
        }
    }
}

// cppcheck-suppress unusedFunction
void app_main(void)
{
    partition_fixup_check();

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "%s v%s (%s %s, IDF %s) starting...",
             app->project_name, app->version, app->date, app->time, app->idf_ver);

    // Suppress noisy wifi debug logs (before wifi_init)
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_ERROR_CHECK(log_stream_init());

    // Initialize NVS (required by WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Load config from NVS (falls back to defaults)
    ESP_ERROR_CHECK(nv_config_init());
    log_reset_reason();
    ESP_ERROR_CHECK(led_init());

    // Boot failure counter — detect WiFi credential boot-loop on no-serial boards
    nv_config_increment_boot_count();
    uint8_t boot_cnt = nv_config_boot_count();
    if (boot_cnt >= NV_CONFIG_BOOT_FAIL_THRESHOLD && nv_config_is_provisioned()) {
        ESP_LOGW(TAG, "boot_count=%" PRIu8 " >= %d: clearing provisioning for AP fallback",
                 boot_cnt, NV_CONFIG_BOOT_FAIL_THRESHOLD);
        nv_config_clear_provisioned();
        nv_config_reset_boot_count();
    } else if (boot_cnt > 1) {
        ESP_LOGW(TAG, "boot_count=%" PRIu8 " (%d until AP fallback)",
                 boot_cnt, NV_CONFIG_BOOT_FAIL_THRESHOLD - boot_cnt);
    }

#ifdef ASIC_BM1370
    // Initialize ASIC before WiFi — freq ramp takes ~8s, runs while WiFi isn't needed yet.
    // Skip if not provisioned (will enter AP mode instead).
    if (nv_config_is_provisioned()) {
        ESP_ERROR_CHECK(asic_init());
    }
#endif

    // Initialize display early so splash is visible during boot.
    // On Bitaxe, display creates I2C bus itself if asic_init() hasn't run.
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(display_show_splash());
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (!nv_config_is_provisioned()) {
        ESP_LOGI(TAG, "entering provisioning mode");
        ESP_ERROR_CHECK(wifi_init_ap());
        ESP_ERROR_CHECK(http_server_start_prov());

        // Show provisioning info on display + solid blue LED
        char ap_ssid[32];
        wifi_prov_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
        ESP_ERROR_CHECK(display_show_prov(ap_ssid, "taipanminer"));
        ESP_ERROR_CHECK(led_set_color(0, 0, 38));

        bool connected = false;
        while (!connected) {
            xEventGroupWaitBits(g_prov_event_group, PROV_DONE_BIT,
                                pdTRUE, pdFALSE, portMAX_DELAY);

            wifi_stop_ap();

            esp_err_t err = wifi_init_sta();
            if (err == ESP_OK) {
                ESP_ERROR_CHECK(display_off());
                ESP_ERROR_CHECK(led_off());
                ESP_LOGI(TAG, "provisioning complete");
                nv_config_set_provisioned();
                nv_config_reset_boot_count();
                connected = true;
            } else {
                ESP_LOGW(TAG, "STA connect failed, re-entering provisioning");
                ESP_ERROR_CHECK(wifi_init_ap());
            }
        }

        http_server_switch_to_mining();
    } else {
        // Normal boot: connect to saved WiFi
        ESP_ERROR_CHECK(wifi_init());
        ESP_ERROR_CHECK(http_server_start());
    }

    // Sync time via SNTP (UTC)
    sntp_init_time();

    // Start mining
    start_mining();

    // Start display status task on Core 0
    xTaskCreatePinnedToCore(display_status_task, "display", 4096, NULL, 2, NULL, 0);
}
