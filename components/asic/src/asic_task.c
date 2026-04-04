#ifdef BOARD_BITAXE_601

#include "asic.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "asic";

esp_err_t asic_init(void)
{
    ESP_LOGI(TAG, "ASIC init stub");
    return ESP_OK;
}

void asic_mining_task(void *arg)
{
    ESP_LOGI(TAG, "ASIC mining task stub — waiting for driver implementation");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

#endif // BOARD_BITAXE_601
