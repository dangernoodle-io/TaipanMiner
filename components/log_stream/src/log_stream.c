#include "log_stream.h"
#include <stdio.h>
#include <string.h>

int log_stream_format(char *out_buf, size_t out_buf_len, const char *fmt, va_list args)
{
    if (!out_buf || out_buf_len == 0) return -1;
    if (!fmt) {
        out_buf[0] = '\0';
        return 0;
    }
    int n = vsnprintf(out_buf, out_buf_len, fmt, args);
    if (n < 0) {
        out_buf[0] = '\0';
        return -1;
    }
    return (n < (int)out_buf_len) ? n : (int)(out_buf_len - 1);
}

#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

static const char *TAG = "log_stream";

#define LOG_STREAM_BUF_BYTES 6144
#define LOG_STREAM_LINE_MAX  192

static uint8_t s_rb_storage[LOG_STREAM_BUF_BYTES];
static StaticRingbuffer_t s_rb_static;
static RingbufHandle_t s_rb = NULL;
static vprintf_like_t s_orig_vprintf = NULL;
static bool s_ready = false;
static uint32_t s_dropped_lines = 0;

static void s_drop_oldest(void)
{
    size_t item_size = 0;
    void *item = xRingbufferReceive(s_rb, &item_size, 0);
    if (item) {
        vRingbufferReturnItem(s_rb, item);
    }
}

static int s_log_vprintf(const char *fmt, va_list args)
{
    // Always forward to original (serial) output
    va_list args_copy;
    va_copy(args_copy, args);
    int result = s_orig_vprintf(fmt, args_copy);
    va_end(args_copy);

    if (!s_rb) return result;

    char line[LOG_STREAM_LINE_MAX];
    int n = vsnprintf(line, sizeof(line), fmt, args);
    if (n <= 0) return result;

    size_t len = (n < (int)sizeof(line)) ? (size_t)n : sizeof(line) - 1;

    // Non-blocking send; drop oldest on overflow (bounded loop)
    if (xRingbufferSend(s_rb, line, len + 1, 0) != pdTRUE) {
        for (int i = 0; i < 8; i++) {
            s_drop_oldest();
            if (xRingbufferSend(s_rb, line, len + 1, 0) == pdTRUE) break;
            if (i == 7) s_dropped_lines++;
        }
    }

    return result;
}

esp_err_t log_stream_init(void)
{
    s_rb = xRingbufferCreateStatic(LOG_STREAM_BUF_BYTES, RINGBUF_TYPE_NOSPLIT,
                                    s_rb_storage, &s_rb_static);
    if (!s_rb) {
        ESP_LOGE(TAG, "ring buffer creation failed");
        return ESP_ERR_NO_MEM;
    }

    s_orig_vprintf = esp_log_set_vprintf(s_log_vprintf);
    s_ready = true;
    ESP_LOGI(TAG, "log stream initialised (%" PRIu32 " bytes)", (uint32_t)LOG_STREAM_BUF_BYTES);
    return ESP_OK;
}

size_t log_stream_drain(char *out_buf, size_t out_buf_len, uint32_t ticks_to_wait)
{
    if (!s_rb || !out_buf || out_buf_len == 0) return 0;

    size_t item_size = 0;
    char *item = xRingbufferReceive(s_rb, &item_size, (TickType_t)ticks_to_wait);
    if (!item) return 0;

    size_t copy_len = (item_size < out_buf_len) ? item_size : out_buf_len - 1;
    memcpy(out_buf, item, copy_len);
    out_buf[copy_len] = '\0';
    vRingbufferReturnItem(s_rb, item);
    return strlen(out_buf);
}

bool log_stream_ready(void)
{
    return s_ready;
}

uint32_t log_stream_dropped_lines(void)
{
    return s_dropped_lines;
}

#endif /* ESP_PLATFORM */
