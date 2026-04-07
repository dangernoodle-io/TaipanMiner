#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
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
#include "partition_fixup.h"
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

static void sntp_init_time(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
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
        }
    }

    // Create inter-task queues
    work_queue = xQueueCreate(1, sizeof(mining_work_t));
    result_queue = xQueueCreate(2, sizeof(mining_result_t));

    // Initialize mining stats
    mining_stats_init();

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

    // Initialize NVS (required by WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Load config from NVS (falls back to defaults)
    ESP_ERROR_CHECK(nv_config_init());
    ESP_ERROR_CHECK(led_init());

#ifdef ASIC_BM1370
    // Initialize ASIC before WiFi — freq ramp takes ~8s, runs while WiFi isn't needed yet.
    // Skip if not provisioned (will enter AP mode instead).
    if (nv_config_is_provisioned()) {
        ESP_ERROR_CHECK(asic_init());
    }
#endif

    // Initialize display early so splash is visible during boot.
    // On Bitaxe, skip if not provisioned — I2C bus not available without asic_init().
#if defined(ASIC_BM1370)
    if (nv_config_is_provisioned()) {
        ESP_ERROR_CHECK(display_init());
        ESP_ERROR_CHECK(display_show_splash());
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_ERROR_CHECK(display_clear(DISPLAY_COLOR_BLACK));
    }
#else
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(display_show_splash());
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_ERROR_CHECK(display_clear(DISPLAY_COLOR_BLACK));
#endif

    if (!nv_config_is_provisioned()) {
        ESP_LOGI(TAG, "entering provisioning mode");
        ESP_ERROR_CHECK(wifi_init_ap());
        ESP_ERROR_CHECK(http_server_start_prov());

        // Show provisioning info on display + solid blue LED
        char ap_ssid[32];
        wifi_prov_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
        ESP_ERROR_CHECK(display_show_prov(ap_ssid, "taipanminer"));
        ESP_ERROR_CHECK(led_set_color(0, 0, 255));

        bool connected = false;
        while (!connected) {
            xEventGroupWaitBits(g_prov_event_group, PROV_DONE_BIT,
                                pdTRUE, pdFALSE, portMAX_DELAY);

            wifi_stop_ap();

            esp_err_t err = wifi_init_sta();
            if (err == ESP_OK) {
                ESP_ERROR_CHECK(display_clear(DISPLAY_COLOR_BLACK));
                ESP_ERROR_CHECK(led_off());
                ESP_LOGI(TAG, "provisioning complete");
                nv_config_set_provisioned();
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
}
