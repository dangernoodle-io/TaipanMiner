#include "wifi_reconn.h"
#include "nv_config.h"

#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "wifi_reconn";

#define RECONN_QUEUE_LEN 8
#define RECONN_TASK_STACK 4096
#define RECONN_TASK_PRIO 6
#define RECONN_TASK_CORE 0

#define HANDSHAKE_BACKOFF_TIER2_MS 10000
#define HANDSHAKE_BACKOFF_TIER3_MS 30000
#define GENERIC_BACKOFF_PAUSE_MS   5000
#define GENERIC_FAST_RETRY_LIMIT   10
#define HANDSHAKE_FAST_RETRY_LIMIT 3
#define HANDSHAKE_TIER2_LIMIT      6
#define PERSISTENT_FAIL_WINDOW_US  (5LL * 60LL * 1000000LL)

typedef enum {
    EVT_DISCONNECT = 1,
    EVT_GOT_IP     = 2,
} reconn_evt_type_t;

typedef struct {
    reconn_evt_type_t type;
    uint8_t reason;
} reconn_evt_t;

typedef enum {
    ST_IDLE,
    ST_BACKOFF,
    ST_CONNECTING,
} reconn_state_t;

// Task + queue handles
static TaskHandle_t  s_task  = NULL;
static QueueHandle_t s_queue = NULL;
static volatile bool s_active = false;

// Manager-owned diagnostic state. Manager task is single writer.
static volatile uint8_t  s_last_reason = 0;
static volatile int64_t  s_last_disconnect_us = 0;
static volatile int      s_retry_count = 0;
static uint16_t          s_reason_histogram[256];  // single-writer, readers use volatile cast

// Failure window tracking (for reboot trigger)
static int     s_handshake_fail_count = 0;
static int     s_generic_fail_count   = 0;
static int64_t s_first_fail_us        = 0;

static void reset_counters_on_success(void)
{
    s_handshake_fail_count = 0;
    s_generic_fail_count   = 0;
    s_first_fail_us        = 0;
    s_retry_count          = 0;
}

static uint32_t compute_backoff_ms(uint8_t reason)
{
    if (reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
        int n = s_handshake_fail_count;
        if (n <= HANDSHAKE_FAST_RETRY_LIMIT)     return 0;
        if (n <= HANDSHAKE_TIER2_LIMIT)          return HANDSHAKE_BACKOFF_TIER2_MS;
        return HANDSHAKE_BACKOFF_TIER3_MS;
    }
    int n = s_generic_fail_count;
    if (n <= GENERIC_FAST_RETRY_LIMIT) return 0;
    return GENERIC_BACKOFF_PAUSE_MS;
}

static bool should_reboot(void)
{
    if (s_first_fail_us == 0) return false;
    int64_t window = esp_timer_get_time() - s_first_fail_us;
    return window > PERSISTENT_FAIL_WINDOW_US;
}

static void handle_disconnect(uint8_t reason, reconn_state_t *state, uint32_t *backoff_ms)
{
    s_last_reason = reason;
    s_last_disconnect_us = esp_timer_get_time();
    if (s_reason_histogram[reason] < UINT16_MAX) {
        s_reason_histogram[reason]++;
    }
    s_retry_count++;

    if (s_first_fail_us == 0) {
        s_first_fail_us = esp_timer_get_time();
    }
    if (reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
        s_handshake_fail_count++;
    } else {
        s_generic_fail_count++;
    }

    if (should_reboot()) {
        ESP_LOGE(TAG, "persistent disconnect for >5min (reason=%u, handshake=%d, generic=%d), rebooting",
                 reason, s_handshake_fail_count, s_generic_fail_count);
        bb_nv_config_increment_boot_count();
        esp_restart();
    }

    *backoff_ms = compute_backoff_ms(reason);
    *state = ST_BACKOFF;
    ESP_LOGW(TAG, "disconnect reason=%u, backoff=%ums (handshake=%d, generic=%d)",
             reason, (unsigned)(*backoff_ms), s_handshake_fail_count, s_generic_fail_count);
}

static void handle_got_ip(reconn_state_t *state, uint32_t *backoff_ms)
{
    ESP_LOGI(TAG, "got ip, reconnect manager idle");
    reset_counters_on_success();
    *backoff_ms = 0;
    *state = ST_IDLE;
}

static void reconn_task(void *arg)
{
    (void)arg;
    reconn_state_t state = ST_IDLE;
    uint32_t backoff_ms = 0;

    ESP_LOGI(TAG, "reconnect manager started");

    for (;;) {
        TickType_t wait;
        switch (state) {
            case ST_BACKOFF:   wait = pdMS_TO_TICKS(backoff_ms); break;
            case ST_IDLE:
            case ST_CONNECTING:
            default:           wait = portMAX_DELAY;            break;
        }

        reconn_evt_t evt;
        BaseType_t got = xQueueReceive(s_queue, &evt, wait);
        if (got == pdTRUE) {
            switch (evt.type) {
                case EVT_DISCONNECT:
                    handle_disconnect(evt.reason, &state, &backoff_ms);
                    if (backoff_ms == 0) {
                        // Immediate retry
                        ESP_LOGI(TAG, "immediate reconnect attempt");
                        esp_wifi_connect();
                        state = ST_CONNECTING;
                    }
                    break;
                case EVT_GOT_IP:
                    handle_got_ip(&state, &backoff_ms);
                    break;
            }
        } else {
            // Backoff elapsed
            ESP_LOGI(TAG, "backoff elapsed, reconnecting");
            esp_wifi_connect();
            state = ST_CONNECTING;
        }
    }
}

void wifi_reconn_start(void)
{
    if (s_active) return;
    s_queue = xQueueCreate(RECONN_QUEUE_LEN, sizeof(reconn_evt_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "failed to create reconn queue");
        return;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(
        reconn_task, "wifi_reconn",
        RECONN_TASK_STACK, NULL,
        RECONN_TASK_PRIO, &s_task,
        RECONN_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create reconn task");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return;
    }
    s_active = true;
}

bool wifi_reconn_is_active(void)
{
    return s_active;
}

void wifi_reconn_on_disconnect(uint8_t reason)
{
    if (!s_queue) return;
    reconn_evt_t evt = { .type = EVT_DISCONNECT, .reason = reason };
    if (xQueueSend(s_queue, &evt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropping disconnect event");
    }
}

void wifi_reconn_on_got_ip(void)
{
    if (!s_queue) return;
    reconn_evt_t evt = { .type = EVT_GOT_IP, .reason = 0 };
    if (xQueueSend(s_queue, &evt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropping got-ip event");
    }
}

void wifi_reconn_get_disconnect(uint8_t *reason, int64_t *age_us)
{
    if (reason) *reason = s_last_reason;
    if (age_us) {
        int64_t t = s_last_disconnect_us;
        *age_us = (t == 0) ? 0 : (esp_timer_get_time() - t);
    }
}

int wifi_reconn_get_retry_count(void)
{
    return s_retry_count;
}

void wifi_reconn_get_histogram(uint16_t *out, size_t len)
{
    if (!out) return;
    if (len > 256) len = 256;
    for (size_t i = 0; i < len; i++) {
        out[i] = s_reason_histogram[i];
    }
}
