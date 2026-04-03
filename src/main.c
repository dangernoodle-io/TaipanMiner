#include <stdio.h>
#include "esp_log.h"
#include "board.h"

static const char *TAG = "stickminer";

void app_main(void)
{
    ESP_LOGI(TAG, "StickMiner %s starting...", STICKMINER_VERSION);
}
