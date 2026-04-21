#include "ota_validator.h"
#include <stdatomic.h>
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "bb_nv.h"

static const char *TAG = "ota_validator";

#define OTA_VALIDATOR_TIMEOUT_US (15 * 60 * 1000000)  // 15 minutes

static bool s_pending = false;
static esp_timer_handle_t s_timer = NULL;
static atomic_bool s_marked_valid = ATOMIC_VAR_INIT(false);

static void mark_valid_internal(const char *reason)
{
    if (atomic_exchange(&s_marked_valid, true)) {
        return;  // Already marked
    }

    esp_ota_mark_app_valid_cancel_rollback();
    bb_nv_config_reset_boot_count();
    ESP_LOGW(TAG, "firmware validated via %s", reason);
}

static void timer_callback(void *arg)
{
    (void)arg;
    mark_valid_internal("sustained stratum");
}

static bool other_slot_has_valid_app(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) return false;
    const esp_partition_t *other = esp_ota_get_next_update_partition(running);
    if (other == NULL) return false;
    esp_app_desc_t desc;
    return esp_ota_get_partition_description(other, &desc) == ESP_OK;
}

void ota_validator_start(void)
{
    esp_ota_img_states_t ota_state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            if (!other_slot_has_valid_app()) {
                ESP_LOGW(TAG, "other OTA slot lacks a valid app — rollback target unsafe, marking valid immediately");
                mark_valid_internal("rollback-unsafe preflight");
                return;
            }
            s_pending = true;
            ESP_LOGI(TAG, "OTA image pending verification");
        }
    }
}

void ota_validator_on_stratum_authorized(void)
{
    if (!s_pending) {
        return;
    }

    if (atomic_load(&s_marked_valid)) {
        return;
    }

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
        ESP_LOGI(TAG, "15-minute mark-valid timer started");
    }
}

void ota_validator_on_share_accepted(void)
{
    if (!s_pending) {
        return;
    }

    // Cancel timer if running
    if (s_timer != NULL) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }

    mark_valid_internal("first share");
}

esp_err_t ota_validator_mark_valid_manual(void)
{
    if (!s_pending || atomic_load(&s_marked_valid)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_timer != NULL) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }

    mark_valid_internal("manual override");
    return ESP_OK;
}

bool ota_validator_is_pending(void)
{
    return s_pending && !atomic_load(&s_marked_valid);
}
