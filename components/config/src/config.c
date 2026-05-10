#include "config.h"
#include <stdio.h>
#include <string.h>
#include "bb_nv.h"
#include "bb_log.h"
#include "bb_manifest.h"
#ifdef ESP_PLATFORM
#include "bb_mdns.h"
#endif

#define NV_NS "taipanminer"

static struct {
    pool_cfg_t pools[POOL_COUNT];
    char hostname[33];
    /* TA-315/TA-352: autofan / PID fields */
    bool     autofan_enabled;
    uint16_t die_target_c;
    uint16_t vr_target_c;
    uint16_t manual_fan_pct;
    uint16_t min_fan_pct;
} s_config;

static const char *TAG = "config";

/* NVS key names indexed by pool slot (0=primary, 1=fallback).
 * String/u16 fields use a "_2" suffix for slot 1.
 * Bool fields use a "2" suffix (no underscore) — NVS schema is fixed. */
static const char * const s_key_host[2]    = { "pool_host",   "pool_host_2"   };
static const char * const s_key_port[2]    = { "pool_port",   "pool_port_2"   };
static const char * const s_key_wallet[2]  = { "wallet_addr", "wallet_addr_2" };
static const char * const s_key_worker[2]  = { "worker",      "worker_2"      };
static const char * const s_key_pass[2]    = { "pool_pass",   "pool_pass_2"   };
static const char * const s_key_enxsub[2]  = { "pool_enxsub", "pool_enxsub2"  };
static const char * const s_key_dcdcb[2]   = { "pool_dcdcb",  "pool_dcdcb2"   };

/* idx=0 → primary (no suffix); idx=1 → fallback ("_2" / "2" suffix). */
static bb_err_t load_pool_slot(int idx, pool_cfg_t *out)
{
    bb_err_t err;

    err = bb_nv_get_str(NV_NS, s_key_host[idx], out->host, sizeof(out->host), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load %s", s_key_host[idx]);
        return err;
    }
    err = bb_nv_get_str(NV_NS, s_key_wallet[idx], out->wallet, sizeof(out->wallet), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load %s", s_key_wallet[idx]);
        return err;
    }
    err = bb_nv_get_str(NV_NS, s_key_worker[idx], out->worker, sizeof(out->worker), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load %s", s_key_worker[idx]);
        return err;
    }
    err = bb_nv_get_str(NV_NS, s_key_pass[idx], out->pass, sizeof(out->pass), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load %s", s_key_pass[idx]);
        return err;
    }
    err = bb_nv_get_u16(NV_NS, s_key_port[idx], &out->port, 0);
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load %s", s_key_port[idx]);
        return err;
    }
    {
        uint8_t v = 0;
        /* TA-306 default OFF, TA-307 default ON */
        if (bb_nv_get_u8(NV_NS, s_key_enxsub[idx], &v, 0) == BB_OK) {
            out->extranonce_subscribe = (v != 0);
        }
        if (bb_nv_get_u8(NV_NS, s_key_dcdcb[idx], &v, 1) == BB_OK) {
            out->decode_coinbase = (v != 0);
        }
    }
    return BB_OK;
}

static bb_err_t save_pool_slot(int idx, const pool_cfg_t *in)
{
    bb_err_t err;

    err = bb_nv_set_str(NV_NS, s_key_host[idx], in->host);
    if (err != BB_OK) return err;
    err = bb_nv_set_u16(NV_NS, s_key_port[idx], in->port);
    if (err != BB_OK) return err;
    err = bb_nv_set_str(NV_NS, s_key_wallet[idx], in->wallet);
    if (err != BB_OK) return err;
    err = bb_nv_set_str(NV_NS, s_key_worker[idx], in->worker);
    if (err != BB_OK) return err;
    err = bb_nv_set_str(NV_NS, s_key_pass[idx], in->pass);
    if (err != BB_OK) return err;
    err = bb_nv_set_u8(NV_NS, s_key_enxsub[idx], in->extranonce_subscribe ? 1 : 0);
    if (err != BB_OK) return err;
    err = bb_nv_set_u8(NV_NS, s_key_dcdcb[idx],  in->decode_coinbase      ? 1 : 0);
    if (err != BB_OK) return err;

    return BB_OK;
}

static bool valid_hostname(const char *s)
{
    if (!s || s[0] == '\0') {
        return false;
    }
    size_t len = strlen(s);
    if (len > 32) {
        return false;
    }
    if (s[0] == '-' || s[len - 1] == '-') {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

bb_err_t config_init(void)
{
#ifdef ESP_PLATFORM
    bb_err_t err;

    err = load_pool_slot(0, &s_config.pools[0]);
    if (err != BB_OK) return err;

    err = load_pool_slot(1, &s_config.pools[1]);
    if (err != BB_OK) return err;

    // Load hostname
    err = bb_nv_get_str(NV_NS, "hostname", s_config.hostname, sizeof(s_config.hostname), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load hostname");
        return err;
    }

#ifdef ESP_PLATFORM
    // Migration: if hostname is empty and worker is set, derive hostname from worker
    if (s_config.hostname[0] == '\0' && s_config.pools[0].worker[0] != '\0') {
        char normalized[64];
        bb_mdns_build_hostname(s_config.pools[0].worker, NULL, normalized, sizeof(normalized));
        if (normalized[0] != '\0') {
            err = config_set_hostname(normalized);
            if (err == BB_OK) {
                bb_log_i(TAG, "migrated hostname from worker: %s", normalized);
            } else {
                bb_log_w(TAG, "failed to migrate hostname from worker");
            }
        }
    }
#endif

    /* TA-315/TA-352: autofan / PID fields */
    {
        uint8_t v = 1;
        if (bb_nv_get_u8(NV_NS, "autofan", &v, 1) == BB_OK) {
            s_config.autofan_enabled = (v != 0);
        } else {
            s_config.autofan_enabled = true;
        }
        /* TA-352: die_target defaults to 60, vr_target defaults to 75.
         * Old NVS key temp_target is abandoned; no migration. */
        uint16_t u16 = 60;
        if (bb_nv_get_u16(NV_NS, "die_target", &u16, 60) == BB_OK) {
            if (u16 < 35) u16 = 35;
            if (u16 > 85) u16 = 85;
        } else {
            u16 = 60;
        }
        s_config.die_target_c = u16;

        u16 = 75;
        if (bb_nv_get_u16(NV_NS, "vr_target", &u16, 75) == BB_OK) {
            if (u16 < 50) u16 = 50;
            if (u16 > 100) u16 = 100;
        } else {
            u16 = 75;
        }
        s_config.vr_target_c = u16;

        u16 = 100;
        if (bb_nv_get_u16(NV_NS, "manual_fan", &u16, 100) == BB_OK) {
            if (u16 > 100) u16 = 100;
        } else {
            u16 = 100;
        }
        s_config.manual_fan_pct = u16;

        u16 = 25;
        if (bb_nv_get_u16(NV_NS, "min_fan", &u16, 25) == BB_OK) {
            if (u16 > 100) u16 = 100;
        } else {
            u16 = 25;
        }
        s_config.min_fan_pct = u16;
    }

    bb_log_i(TAG, "pool config loaded (pool=%s:%u worker=%s.%s)",
             s_config.pools[0].host, s_config.pools[0].port,
             s_config.pools[0].wallet, s_config.pools[0].worker);
    if (config_pool_configured(POOL_FALLBACK)) {
        bb_log_i(TAG, "fallback pool configured (pool=%s:%u worker=%s.%s)",
                 s_config.pools[1].host, s_config.pools[1].port,
                 s_config.pools[1].wallet, s_config.pools[1].worker);
    }
    return 0;
#else
    memset(&s_config, 0, sizeof(s_config));
    /* Host default: decode_coinbase ON for both slots, matching ESP load. */
    s_config.pools[0].decode_coinbase = true;
    s_config.pools[1].decode_coinbase = true;
    /* TA-315/TA-352: autofan defaults for host */
    s_config.autofan_enabled = true;
    s_config.die_target_c    = 60;
    s_config.vr_target_c     = 75;
    s_config.manual_fan_pct  = 100;
    s_config.min_fan_pct     = 25;
    return 0;
#endif
}

const char *config_pool_host_idx(int idx)
{
    if (idx < 0 || idx >= POOL_COUNT) {
        return "";
    }
    return s_config.pools[idx].host;
}

uint16_t config_pool_port_idx(int idx)
{
    if (idx < 0 || idx >= POOL_COUNT) {
        return 0;
    }
    return s_config.pools[idx].port;
}

const char *config_wallet_addr_idx(int idx)
{
    if (idx < 0 || idx >= POOL_COUNT) {
        return "";
    }
    return s_config.pools[idx].wallet;
}

const char *config_worker_name_idx(int idx)
{
    if (idx < 0 || idx >= POOL_COUNT) {
        return "";
    }
    return s_config.pools[idx].worker;
}

const char *config_pool_pass_idx(int idx)
{
    if (idx < 0 || idx >= POOL_COUNT) {
        return "";
    }
    return s_config.pools[idx].pass;
}

bool config_pool_extranonce_subscribe_idx(int idx)
{
    if (idx < 0 || idx >= POOL_COUNT) {
        return false;
    }
    return s_config.pools[idx].extranonce_subscribe;
}

bool config_pool_decode_coinbase_idx(int idx)
{
    if (idx < 0 || idx >= POOL_COUNT) {
        return true;  /* default-on; bounds-safe fallback matches expected UX */
    }
    return s_config.pools[idx].decode_coinbase;
}

bool config_pool_configured(int idx)
{
    if (idx < 0 || idx >= POOL_COUNT) {
        return false;
    }
    return s_config.pools[idx].host[0] != '\0' &&
           s_config.pools[idx].port != 0 &&
           s_config.pools[idx].wallet[0] != '\0' &&
           s_config.pools[idx].worker[0] != '\0';
}

// Legacy primary-only accessors (aliases for index 0)
const char *config_pool_host(void) { return config_pool_host_idx(POOL_PRIMARY); }
uint16_t config_pool_port(void) { return config_pool_port_idx(POOL_PRIMARY); }
const char *config_wallet_addr(void) { return config_wallet_addr_idx(POOL_PRIMARY); }
const char *config_worker_name(void) { return config_worker_name_idx(POOL_PRIMARY); }
const char *config_pool_pass(void) { return config_pool_pass_idx(POOL_PRIMARY); }
const char *config_hostname(void) { return s_config.hostname; }

bb_err_t config_set_pools(const pool_cfg_t *primary,
                                 const pool_cfg_t *fallback)
{
    if (!primary) {
        return BB_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    bb_err_t err;

    err = save_pool_slot(0, primary);
    if (err != BB_OK) return err;

    if (fallback) {
        err = save_pool_slot(1, fallback);
        if (err != BB_OK) return err;
    } else {
        // Clear fallback by writing empty strings, 0 port, and default bools.
        static const pool_cfg_t empty_fallback = {
            .host   = "",
            .port   = 0,
            .wallet = "",
            .worker = "",
            .pass   = "",
            .extranonce_subscribe = false,
            .decode_coinbase      = true,  /* Reset to default so a re-add starts clean. */
        };
        err = save_pool_slot(1, &empty_fallback);
        if (err != BB_OK) return err;
    }
#endif

    // Update in-memory cache on success
    strncpy(s_config.pools[0].host, primary->host, sizeof(s_config.pools[0].host) - 1);
    s_config.pools[0].host[sizeof(s_config.pools[0].host) - 1] = '\0';
    s_config.pools[0].port = primary->port;
    strncpy(s_config.pools[0].wallet, primary->wallet, sizeof(s_config.pools[0].wallet) - 1);
    s_config.pools[0].wallet[sizeof(s_config.pools[0].wallet) - 1] = '\0';
    strncpy(s_config.pools[0].worker, primary->worker, sizeof(s_config.pools[0].worker) - 1);
    s_config.pools[0].worker[sizeof(s_config.pools[0].worker) - 1] = '\0';
    strncpy(s_config.pools[0].pass, primary->pass, sizeof(s_config.pools[0].pass) - 1);
    s_config.pools[0].pass[sizeof(s_config.pools[0].pass) - 1] = '\0';
    s_config.pools[0].extranonce_subscribe = primary->extranonce_subscribe;
    s_config.pools[0].decode_coinbase      = primary->decode_coinbase;

    if (fallback) {
        strncpy(s_config.pools[1].host, fallback->host, sizeof(s_config.pools[1].host) - 1);
        s_config.pools[1].host[sizeof(s_config.pools[1].host) - 1] = '\0';
        s_config.pools[1].port = fallback->port;
        strncpy(s_config.pools[1].wallet, fallback->wallet, sizeof(s_config.pools[1].wallet) - 1);
        s_config.pools[1].wallet[sizeof(s_config.pools[1].wallet) - 1] = '\0';
        strncpy(s_config.pools[1].worker, fallback->worker, sizeof(s_config.pools[1].worker) - 1);
        s_config.pools[1].worker[sizeof(s_config.pools[1].worker) - 1] = '\0';
        strncpy(s_config.pools[1].pass, fallback->pass, sizeof(s_config.pools[1].pass) - 1);
        s_config.pools[1].pass[sizeof(s_config.pools[1].pass) - 1] = '\0';
        s_config.pools[1].extranonce_subscribe = fallback->extranonce_subscribe;
        s_config.pools[1].decode_coinbase      = fallback->decode_coinbase;
    } else {
        memset(&s_config.pools[1], 0, sizeof(s_config.pools[1]));
        /* Reset fallback bools to their defaults on clear. */
        s_config.pools[1].decode_coinbase = true;
    }

    return BB_OK;
}

bb_err_t config_set_pool(const char *pool_host, uint16_t pool_port,
                                      const char *wallet_addr, const char *worker_name,
                                      const char *pool_pass)
{
    // Build primary pool config
    pool_cfg_t primary = {0};
    strncpy(primary.host, pool_host, sizeof(primary.host) - 1);
    primary.host[sizeof(primary.host) - 1] = '\0';
    primary.port = pool_port;
    strncpy(primary.wallet, wallet_addr, sizeof(primary.wallet) - 1);
    primary.wallet[sizeof(primary.wallet) - 1] = '\0';
    strncpy(primary.worker, worker_name, sizeof(primary.worker) - 1);
    primary.worker[sizeof(primary.worker) - 1] = '\0';
    strncpy(primary.pass, pool_pass, sizeof(primary.pass) - 1);
    primary.pass[sizeof(primary.pass) - 1] = '\0';
    /* Preserve existing primary toggle state — the legacy setter doesn't
     * accept these fields. */
    primary.extranonce_subscribe = s_config.pools[0].extranonce_subscribe;
    primary.decode_coinbase      = s_config.pools[0].decode_coinbase;

    // Read current fallback from cache
    pool_cfg_t current_fallback = s_config.pools[1];

    // Only pass fallback if it's configured; otherwise pass NULL to clear it
    pool_cfg_t *fallback_ptr = config_pool_configured(POOL_FALLBACK) ? &current_fallback : NULL;

    return config_set_pools(&primary, fallback_ptr);
}

bb_err_t config_set_hostname(const char *hostname)
{
    if (!valid_hostname(hostname)) {
        return BB_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    bb_err_t err = bb_nv_set_str(NV_NS, "hostname", hostname);
    if (err != BB_OK) {
        return err;
    }
#endif

    // Update in-memory cache on success
    strncpy(s_config.hostname, hostname, sizeof(s_config.hostname) - 1);
    s_config.hostname[sizeof(s_config.hostname) - 1] = '\0';

    return BB_OK;
}

/* TA-315/TA-352: autofan / PID getters */
bool     config_autofan_enabled(void) { return s_config.autofan_enabled; }
uint16_t config_die_target_c(void)    { return s_config.die_target_c; }
uint16_t config_vr_target_c(void)     { return s_config.vr_target_c; }
uint16_t config_manual_fan_pct(void)  { return s_config.manual_fan_pct; }
uint16_t config_min_fan_pct(void)     { return s_config.min_fan_pct; }

bb_err_t config_set_autofan_enabled(bool enabled)
{
#ifdef ESP_PLATFORM
    bb_err_t err = bb_nv_set_u8(NV_NS, "autofan", enabled ? 1 : 0);
    if (err != BB_OK) return err;
#endif
    s_config.autofan_enabled = enabled;
    return BB_OK;
}

bb_err_t config_set_die_target_c(uint16_t val)
{
    if (val < 35) val = 35;
    if (val > 85) val = 85;
#ifdef ESP_PLATFORM
    bb_err_t err = bb_nv_set_u16(NV_NS, "die_target", val);
    if (err != BB_OK) return err;
#endif
    s_config.die_target_c = val;
    return BB_OK;
}

bb_err_t config_set_vr_target_c(uint16_t val)
{
    if (val < 50) val = 50;
    if (val > 100) val = 100;
#ifdef ESP_PLATFORM
    bb_err_t err = bb_nv_set_u16(NV_NS, "vr_target", val);
    if (err != BB_OK) return err;
#endif
    s_config.vr_target_c = val;
    return BB_OK;
}

bb_err_t config_set_manual_fan_pct(uint16_t val)
{
    if (val > 100) val = 100;
#ifdef ESP_PLATFORM
    bb_err_t err = bb_nv_set_u16(NV_NS, "manual_fan", val);
    if (err != BB_OK) return err;
#endif
    s_config.manual_fan_pct = val;
    return BB_OK;
}

bb_err_t config_set_min_fan_pct(uint16_t val)
{
    if (val > 100) val = 100;
#ifdef ESP_PLATFORM
    bb_err_t err = bb_nv_set_u16(NV_NS, "min_fan", val);
    if (err != BB_OK) return err;
#endif
    s_config.min_fan_pct = val;
    return BB_OK;
}

bb_err_t config_register_manifest(void)
{
    static const bb_manifest_nv_t nv_keys[] = {
        {
            .key = "pool_host",
            .type = "str",
            .default_ = "",
            .max_len = 63,
            .desc = "pool hostname or IP address",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "pool_port",
            .type = "u16",
            .default_ = "0",
            .max_len = 0,
            .desc = "pool port",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "wallet_addr",
            .type = "str",
            .default_ = "",
            .max_len = 63,
            .desc = "wallet address",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "worker",
            .type = "str",
            .default_ = "",
            .max_len = 31,
            .desc = "worker name",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "pool_pass",
            .type = "str",
            .default_ = "",
            .max_len = 63,
            .desc = "pool password",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "hostname",
            .type = "str",
            .default_ = "",
            .max_len = 32,
            .desc = "device hostname",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "pool_host_2",
            .type = "str",
            .default_ = "",
            .max_len = 63,
            .desc = "fallback pool hostname or IP address",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "pool_port_2",
            .type = "u16",
            .default_ = "0",
            .max_len = 0,
            .desc = "fallback pool port",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "wallet_addr_2",
            .type = "str",
            .default_ = "",
            .max_len = 63,
            .desc = "fallback wallet address",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "worker_2",
            .type = "str",
            .default_ = "",
            .max_len = 31,
            .desc = "fallback worker name",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "pool_pass_2",
            .type = "str",
            .default_ = "",
            .max_len = 63,
            .desc = "fallback pool password",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "pool_enxsub",
            .type = "u8",
            .default_ = "0",
            .max_len = 0,
            .desc = "primary: send mining.extranonce.subscribe (TA-306)",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "pool_enxsub2",
            .type = "u8",
            .default_ = "0",
            .max_len = 0,
            .desc = "fallback: send mining.extranonce.subscribe (TA-306)",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "pool_dcdcb",
            .type = "u8",
            .default_ = "1",
            .max_len = 0,
            .desc = "primary: UI decodes coinbase tx (TA-307)",
            .reboot_required = false,
            .provisioning_only = false,
        },
        {
            .key = "pool_dcdcb2",
            .type = "u8",
            .default_ = "1",
            .max_len = 0,
            .desc = "fallback: UI decodes coinbase tx (TA-307)",
            .reboot_required = false,
            .provisioning_only = false,
        },
    };

    return bb_manifest_register_nv(NV_NS, nv_keys,
                                   sizeof(nv_keys) / sizeof(nv_keys[0]));
}
