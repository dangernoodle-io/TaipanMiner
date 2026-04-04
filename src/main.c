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
#include "esp_ota_ops.h"

static const char *TAG = "taipanminer";

// Mining task handles (for suspend/resume during OTA verification)
TaskHandle_t mining_hw_task_handle = NULL;
TaskHandle_t mining_sw_task_handle = NULL;

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

    // Start mining task on Core 1 (hardware SHA)
    xTaskCreatePinnedToCore(mining_task, "mining_hw", 4096, NULL, 20, &mining_hw_task_handle, 1);

    // Start software mining task on Core 0 (software SHA, lower priority than stratum)
    xTaskCreatePinnedToCore(mining_task_sw, "mining_sw", 4096, NULL, 3, &mining_sw_task_handle, 0);

    ESP_LOGI(TAG, "all tasks started");
}

// cppcheck-suppress unusedFunction
void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "TaipanMiner (%s) starting...", app->version);

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

    if (!nv_config_is_provisioned()) {
        // Provisioning mode: start AP + captive portal
        ESP_LOGI(TAG, "entering provisioning mode");
        ESP_ERROR_CHECK(wifi_init_ap());
        ESP_ERROR_CHECK(http_server_start_prov());

        bool connected = false;
        while (!connected) {
            xEventGroupWaitBits(g_prov_event_group, PROV_DONE_BIT,
                                pdTRUE, pdFALSE, portMAX_DELAY);

            wifi_stop_ap();

            esp_err_t err = wifi_init_sta();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "provisioning complete");
                nv_config_set_provisioned();
                connected = true;
            } else {
                ESP_LOGW(TAG, "STA connect failed, re-entering provisioning");
                ESP_ERROR_CHECK(wifi_init_ap());
            }
        }

        // Switch HTTP server from provisioning to mining handlers
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
