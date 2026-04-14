#pragma once

#ifdef ESP_PLATFORM

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// RGB565 colors (standard, not byte-swapped)
#define DISPLAY_COLOR_BLACK   0x0000
#define DISPLAY_COLOR_WHITE   0xFFFF
#define DISPLAY_COLOR_GREEN   0x07E0
#define DISPLAY_COLOR_CYAN    0x07FF
#define DISPLAY_COLOR_YELLOW  0xFFE0
#define DISPLAY_COLOR_AMBER      0xC3A0
#define DISPLAY_COLOR_DARK_AMBER 0x6200

typedef struct {
    double   hashrate;     // EMA H/s
    float    temp_c;       // relevant temp for board
    uint32_t shares;       // session accepted
    uint32_t rejected;     // session rejected
    int64_t  uptime_us;    // session uptime in microseconds
    // Network diagnostics
    int8_t   rssi;               // 0 if not available
    char     ip[16];             // dotted string
    uint8_t  wifi_disc_reason;   // 0 = never
    uint32_t wifi_disc_age_s;    // seconds since last disconnect
    int      wifi_retry_count;
    bool     mdns_ok;
    bool     stratum_ok;
    uint32_t stratum_reconnect_ms;
    int      stratum_fail_count;
} display_status_t;

esp_err_t display_init(void);
esp_err_t display_clear(uint16_t color);
esp_err_t display_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg);
esp_err_t display_show_splash(void);
esp_err_t display_show_prov(const char *ssid, const char *password);
esp_err_t display_off(void);
esp_err_t display_show_status(const display_status_t *status);

#endif
