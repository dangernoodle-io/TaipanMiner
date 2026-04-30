#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "bb_nv.h"

bb_err_t taipan_config_init(void);
bb_err_t taipan_config_register_manifest(void);

const char *taipan_config_pool_host(void);
uint16_t taipan_config_pool_port(void);
const char *taipan_config_wallet_addr(void);
const char *taipan_config_worker_name(void);
const char *taipan_config_pool_pass(void);
const char *taipan_config_hostname(void);

bb_err_t taipan_config_set_pool(const char *pool_host, uint16_t pool_port,
                                      const char *wallet_addr, const char *worker_name,
                                      const char *pool_pass);
bb_err_t taipan_config_set_hostname(const char *hostname);

#define TAIPAN_POOL_PRIMARY  0
#define TAIPAN_POOL_FALLBACK 1
#define TAIPAN_POOL_COUNT    2

typedef struct {
    char host[64];
    uint16_t port;
    char wallet[64];
    char worker[32];
    char pass[64];
} taipan_pool_cfg_t;

/* Per-index getters. idx must be 0 or 1. Returns "" or 0 for unset. */
const char *taipan_config_pool_host_idx(int idx);
uint16_t    taipan_config_pool_port_idx(int idx);
const char *taipan_config_wallet_addr_idx(int idx);
const char *taipan_config_worker_name_idx(int idx);
const char *taipan_config_pool_pass_idx(int idx);

/* True iff a pool is configured at this idx (host + port + wallet + worker all set). */
bool taipan_config_pool_configured(int idx);

/* Atomic write: sets primary (required) and fallback (NULL clears fallback). */
bb_err_t taipan_config_set_pools(const taipan_pool_cfg_t *primary,
                                 const taipan_pool_cfg_t *fallback);
