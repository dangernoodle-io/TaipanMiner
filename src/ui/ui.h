#pragma once

#ifdef ESP_PLATFORM

#include "bb_nv.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    double   hashrate;
    float    temp_c;
    uint32_t shares;
    uint32_t rejected;
    int64_t  uptime_us;
    int8_t   rssi;
    char     ip[16];
    uint8_t  wifi_disc_reason;
    uint32_t wifi_disc_age_s;
    int      wifi_retry_count;
    bool     mdns_ok;
    bool     stratum_ok;
    uint32_t stratum_reconnect_ms;
    int      stratum_fail_count;
} display_status_t;

void ui_show_splash(void);
void ui_show_prov(const char *ssid, const char *password);
void ui_show_status(const display_status_t *status);
void ui_display_off(void);
void ui_display_on(void);

#endif
