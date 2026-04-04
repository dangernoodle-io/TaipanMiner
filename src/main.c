#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "board.h"
#include "wifi_prov.h"
#include "mining.h"
#include "work.h"
#include "stratum.h"

static const char *TAG = "stickminer";

void app_main(void)
{
    ESP_LOGI(TAG, "StickMiner %s starting...", STICKMINER_VERSION);

    // Initialize NVS (required by WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Connect WiFi (blocks until connected)
    ESP_ERROR_CHECK(wifi_init());

    // Suppress noisy wifi debug logs
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);

    // Create inter-task queues
    work_queue = xQueueCreate(1, sizeof(mining_work_t));
    result_queue = xQueueCreate(4, sizeof(mining_result_t));

    // Start stratum task on Core 0
    xTaskCreatePinnedToCore(stratum_task, "stratum", 12288, NULL, 5, NULL, 0);

    // Start mining task on Core 1 (hardware SHA)
    xTaskCreatePinnedToCore(mining_task, "mining_hw", 4096, NULL, 20, NULL, 1);

    // Start software mining task on Core 0 (software SHA, lower priority than stratum)
    xTaskCreatePinnedToCore(mining_task_sw, "mining_sw", 4096, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "all tasks started");
}
