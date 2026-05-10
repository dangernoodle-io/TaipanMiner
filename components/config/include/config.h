#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "bb_nv.h"

bb_err_t config_init(void);
bb_err_t config_register_manifest(void);

const char *config_pool_host(void);
uint16_t config_pool_port(void);
const char *config_wallet_addr(void);
const char *config_worker_name(void);
const char *config_pool_pass(void);
const char *config_hostname(void);

bb_err_t config_set_pool(const char *pool_host, uint16_t pool_port,
                                      const char *wallet_addr, const char *worker_name,
                                      const char *pool_pass);
bb_err_t config_set_hostname(const char *hostname);

#define POOL_PRIMARY  0
#define POOL_FALLBACK 1
#define POOL_COUNT    2

typedef struct {
    char host[64];
    uint16_t port;
    char wallet[64];
    char worker[32];
    char pass[64];
    /* TA-306: whether to send mining.extranonce.subscribe after authorize. */
    bool extranonce_subscribe;
    /* TA-307: whether the UI should attempt to decode coinbase tx fields
     * (block height, scriptSig tag, payout, reward) for this pool. Default
     * true; set false for non-BTC SHA-256d pools whose coinbase doesn't
     * follow BIP34. Firmware just persists + exposes; UI consults. */
    bool decode_coinbase;
} pool_cfg_t;

/* Per-index getters. idx must be 0 or 1. Returns "" or 0 for unset. */
const char *config_pool_host_idx(int idx);
uint16_t    config_pool_port_idx(int idx);
const char *config_wallet_addr_idx(int idx);
const char *config_worker_name_idx(int idx);
const char *config_pool_pass_idx(int idx);
bool        config_pool_extranonce_subscribe_idx(int idx);
bool        config_pool_decode_coinbase_idx(int idx);

/* True iff a pool is configured at this idx (host + port + wallet + worker all set). */
bool config_pool_configured(int idx);

/* Atomic write: sets primary (required) and fallback (NULL clears fallback). */
bb_err_t config_set_pools(const pool_cfg_t *primary,
                                 const pool_cfg_t *fallback);

/* TA-315/TA-352: autofan / PID config */
bool     config_autofan_enabled(void);
uint16_t config_die_target_c(void);
uint16_t config_vr_target_c(void);
uint16_t config_manual_fan_pct(void);
uint16_t config_min_fan_pct(void);

bb_err_t config_set_autofan_enabled(bool enabled);
bb_err_t config_set_die_target_c(uint16_t val);
bb_err_t config_set_vr_target_c(uint16_t val);
bb_err_t config_set_manual_fan_pct(uint16_t val);
bb_err_t config_set_min_fan_pct(uint16_t val);
