#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Format fmt+args into out_buf (like vsnprintf).
 * Platform-independent — exposed for host tests.
 * Returns number of bytes written (excluding NUL), or negative on error.
 */
int log_stream_format(char *out_buf, size_t out_buf_len, const char *fmt, va_list args);

#ifdef ESP_PLATFORM
#include "esp_err.h"

/**
 * Initialise the ring buffer and install the custom vprintf hook.
 * Must be called once from app_main before any tasks are started.
 */
esp_err_t log_stream_init(void);

/**
 * Drain one log line from the ring buffer into out_buf.
 * Blocks for up to ticks_to_wait FreeRTOS ticks.
 * Returns the number of bytes written (excluding NUL), or 0 on timeout.
 */
size_t log_stream_drain(char *out_buf, size_t out_buf_len, uint32_t ticks_to_wait);

/**
 * Returns true if log_stream_init() has been called successfully.
 */
bool log_stream_ready(void);

/**
 * Returns the count of lines that could not be sent despite the drop-oldest loop.
 */
uint32_t log_stream_dropped_lines(void);

#endif /* ESP_PLATFORM */
