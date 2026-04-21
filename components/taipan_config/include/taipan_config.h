#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
typedef esp_err_t taipan_err_t;
#else
typedef int taipan_err_t;
#endif

taipan_err_t taipan_config_init(void);

const char *taipan_config_pool_host(void);
uint16_t taipan_config_pool_port(void);
const char *taipan_config_wallet_addr(void);
const char *taipan_config_worker_name(void);
const char *taipan_config_pool_pass(void);

#ifdef ESP_PLATFORM
taipan_err_t taipan_config_set_pool(const char *pool_host, uint16_t pool_port,
                                      const char *wallet_addr, const char *worker_name,
                                      const char *pool_pass);
#endif
