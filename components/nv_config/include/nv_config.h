#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
#define ESP_OK 0
typedef int esp_err_t;
#endif

esp_err_t nv_config_init(void);

const char *nv_config_wifi_ssid(void);
const char *nv_config_wifi_pass(void);
const char *nv_config_pool_host(void);
uint16_t nv_config_pool_port(void);
const char *nv_config_wallet_addr(void);
const char *nv_config_worker_name(void);
const char *nv_config_pool_pass(void);

#ifdef ESP_PLATFORM
bool nv_config_is_provisioned(void);
esp_err_t nv_config_set_provisioned(void);
esp_err_t nv_config_set_wifi(const char *ssid, const char *pass);
esp_err_t nv_config_set_config(const char *pool_host, uint16_t pool_port,
                                const char *wallet_addr, const char *worker_name,
                                const char *pool_pass);
#else
static inline bool nv_config_is_provisioned(void) { return false; }
#endif
