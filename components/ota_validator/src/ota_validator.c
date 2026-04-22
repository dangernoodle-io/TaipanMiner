#include "ota_validator.h"
#include <stdatomic.h>
#include "esp_timer.h"
#include "bb_log.h"
#include "bb_ota_validator.h"

static const char *TAG = "ota_validator";

#define OTA_VALIDATOR_TIMEOUT_US (15 * 60 * 1000000)  // 15 minutes

static esp_timer_handle_t s_timer = NULL;

static void timer_callback(void *arg)
{
    (void)arg;
    bb_ota_mark_valid("sustained stratum");
}

void ota_validator_on_stratum_authorized(void)
{
    if (s_timer != NULL) {
        return;  // Timer already running
    }

    const esp_timer_create_args_t timer_args = {
        .callback = timer_callback,
        .arg = NULL,
        .name = "ota_validator",
        .dispatch_method = ESP_TIMER_TASK,
    };

    if (esp_timer_create(&timer_args, &s_timer) == ESP_OK) {
        esp_timer_start_once(s_timer, OTA_VALIDATOR_TIMEOUT_US);
        bb_log_i(TAG, "15-minute mark-valid timer started");
    }
}

void ota_validator_on_share_accepted(void)
{
    // Cancel timer if running
    if (s_timer != NULL) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }

    bb_ota_mark_valid("first share");
}
