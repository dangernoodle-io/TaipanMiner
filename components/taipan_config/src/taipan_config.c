#include "taipan_config.h"
#include <string.h>
#include "bb_nv.h"
#include "bb_log.h"
#include "bb_manifest.h"
#ifdef ESP_PLATFORM
#include "bb_mdns.h"
#endif

#define TAIPAN_NS "taipanminer"

static struct {
    taipan_pool_cfg_t pools[TAIPAN_POOL_COUNT];
    char hostname[33];
} s_config;

static const char *TAG = "taipan_config";

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

bb_err_t taipan_config_init(void)
{
#ifdef ESP_PLATFORM
    bb_err_t err;

    // Load primary pool (index 0)
    err = bb_nv_get_str(TAIPAN_NS, "pool_host", s_config.pools[0].host, sizeof(s_config.pools[0].host), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load pool_host");
        return err;
    }

    err = bb_nv_get_str(TAIPAN_NS, "wallet_addr", s_config.pools[0].wallet, sizeof(s_config.pools[0].wallet), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load wallet_addr");
        return err;
    }

    err = bb_nv_get_str(TAIPAN_NS, "worker", s_config.pools[0].worker, sizeof(s_config.pools[0].worker), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load worker");
        return err;
    }

    err = bb_nv_get_str(TAIPAN_NS, "pool_pass", s_config.pools[0].pass, sizeof(s_config.pools[0].pass), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load pool_pass");
        return err;
    }

    err = bb_nv_get_u16(TAIPAN_NS, "pool_port", &s_config.pools[0].port, 0);
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load pool_port");
        return err;
    }

    {
        uint8_t v = 0;
        /* TA-306 default OFF, TA-307 default ON */
        if (bb_nv_get_u8(TAIPAN_NS, "pool_enxsub",  &v, 0) == BB_OK) {
            s_config.pools[0].extranonce_subscribe = (v != 0);
        }
        if (bb_nv_get_u8(TAIPAN_NS, "pool_dcdcb",   &v, 1) == BB_OK) {
            s_config.pools[0].decode_coinbase = (v != 0);
        }
    }

    // Load fallback pool (index 1)
    err = bb_nv_get_str(TAIPAN_NS, "pool_host_2", s_config.pools[1].host, sizeof(s_config.pools[1].host), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load pool_host_2");
        return err;
    }

    err = bb_nv_get_str(TAIPAN_NS, "wallet_addr_2", s_config.pools[1].wallet, sizeof(s_config.pools[1].wallet), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load wallet_addr_2");
        return err;
    }

    err = bb_nv_get_str(TAIPAN_NS, "worker_2", s_config.pools[1].worker, sizeof(s_config.pools[1].worker), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load worker_2");
        return err;
    }

    err = bb_nv_get_str(TAIPAN_NS, "pool_pass_2", s_config.pools[1].pass, sizeof(s_config.pools[1].pass), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load pool_pass_2");
        return err;
    }

    err = bb_nv_get_u16(TAIPAN_NS, "pool_port_2", &s_config.pools[1].port, 0);
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load pool_port_2");
        return err;
    }

    {
        uint8_t v = 0;
        if (bb_nv_get_u8(TAIPAN_NS, "pool_enxsub2", &v, 0) == BB_OK) {
            s_config.pools[1].extranonce_subscribe = (v != 0);
        }
        if (bb_nv_get_u8(TAIPAN_NS, "pool_dcdcb2",  &v, 1) == BB_OK) {
            s_config.pools[1].decode_coinbase = (v != 0);
        }
    }

    // Load hostname
    err = bb_nv_get_str(TAIPAN_NS, "hostname", s_config.hostname, sizeof(s_config.hostname), "");
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
            err = taipan_config_set_hostname(normalized);
            if (err == BB_OK) {
                bb_log_i(TAG, "migrated hostname from worker: %s", normalized);
            } else {
                bb_log_w(TAG, "failed to migrate hostname from worker");
            }
        }
    }
#endif

    bb_log_i(TAG, "pool config loaded (pool=%s:%u worker=%s.%s)",
             s_config.pools[0].host, s_config.pools[0].port,
             s_config.pools[0].wallet, s_config.pools[0].worker);
    if (taipan_config_pool_configured(TAIPAN_POOL_FALLBACK)) {
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
    return 0;
#endif
}

const char *taipan_config_pool_host_idx(int idx)
{
    if (idx < 0 || idx >= TAIPAN_POOL_COUNT) {
        return "";
    }
    return s_config.pools[idx].host;
}

uint16_t taipan_config_pool_port_idx(int idx)
{
    if (idx < 0 || idx >= TAIPAN_POOL_COUNT) {
        return 0;
    }
    return s_config.pools[idx].port;
}

const char *taipan_config_wallet_addr_idx(int idx)
{
    if (idx < 0 || idx >= TAIPAN_POOL_COUNT) {
        return "";
    }
    return s_config.pools[idx].wallet;
}

const char *taipan_config_worker_name_idx(int idx)
{
    if (idx < 0 || idx >= TAIPAN_POOL_COUNT) {
        return "";
    }
    return s_config.pools[idx].worker;
}

const char *taipan_config_pool_pass_idx(int idx)
{
    if (idx < 0 || idx >= TAIPAN_POOL_COUNT) {
        return "";
    }
    return s_config.pools[idx].pass;
}

bool taipan_config_pool_extranonce_subscribe_idx(int idx)
{
    if (idx < 0 || idx >= TAIPAN_POOL_COUNT) {
        return false;
    }
    return s_config.pools[idx].extranonce_subscribe;
}

bool taipan_config_pool_decode_coinbase_idx(int idx)
{
    if (idx < 0 || idx >= TAIPAN_POOL_COUNT) {
        return true;  /* default-on; bounds-safe fallback matches expected UX */
    }
    return s_config.pools[idx].decode_coinbase;
}

bool taipan_config_pool_configured(int idx)
{
    if (idx < 0 || idx >= TAIPAN_POOL_COUNT) {
        return false;
    }
    return s_config.pools[idx].host[0] != '\0' &&
           s_config.pools[idx].port != 0 &&
           s_config.pools[idx].wallet[0] != '\0' &&
           s_config.pools[idx].worker[0] != '\0';
}

// Legacy primary-only accessors (aliases for index 0)
const char *taipan_config_pool_host(void) { return taipan_config_pool_host_idx(TAIPAN_POOL_PRIMARY); }
uint16_t taipan_config_pool_port(void) { return taipan_config_pool_port_idx(TAIPAN_POOL_PRIMARY); }
const char *taipan_config_wallet_addr(void) { return taipan_config_wallet_addr_idx(TAIPAN_POOL_PRIMARY); }
const char *taipan_config_worker_name(void) { return taipan_config_worker_name_idx(TAIPAN_POOL_PRIMARY); }
const char *taipan_config_pool_pass(void) { return taipan_config_pool_pass_idx(TAIPAN_POOL_PRIMARY); }
const char *taipan_config_hostname(void) { return s_config.hostname; }

bb_err_t taipan_config_set_pools(const taipan_pool_cfg_t *primary,
                                 const taipan_pool_cfg_t *fallback)
{
    if (!primary) {
        return BB_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    bb_err_t err;

    // Write primary pool (index 0)
    err = bb_nv_set_str(TAIPAN_NS, "pool_host", primary->host);
    if (err != BB_OK) return err;

    err = bb_nv_set_u16(TAIPAN_NS, "pool_port", primary->port);
    if (err != BB_OK) return err;

    err = bb_nv_set_str(TAIPAN_NS, "wallet_addr", primary->wallet);
    if (err != BB_OK) return err;

    err = bb_nv_set_str(TAIPAN_NS, "worker", primary->worker);
    if (err != BB_OK) return err;

    err = bb_nv_set_str(TAIPAN_NS, "pool_pass", primary->pass);
    if (err != BB_OK) return err;

    err = bb_nv_set_u8(TAIPAN_NS, "pool_enxsub", primary->extranonce_subscribe ? 1 : 0);
    if (err != BB_OK) return err;
    err = bb_nv_set_u8(TAIPAN_NS, "pool_dcdcb",  primary->decode_coinbase      ? 1 : 0);
    if (err != BB_OK) return err;

    // Write fallback pool (index 1) or clear if NULL
    if (fallback) {
        err = bb_nv_set_str(TAIPAN_NS, "pool_host_2", fallback->host);
        if (err != BB_OK) return err;

        err = bb_nv_set_u16(TAIPAN_NS, "pool_port_2", fallback->port);
        if (err != BB_OK) return err;

        err = bb_nv_set_str(TAIPAN_NS, "wallet_addr_2", fallback->wallet);
        if (err != BB_OK) return err;

        err = bb_nv_set_str(TAIPAN_NS, "worker_2", fallback->worker);
        if (err != BB_OK) return err;

        err = bb_nv_set_str(TAIPAN_NS, "pool_pass_2", fallback->pass);
        if (err != BB_OK) return err;

        err = bb_nv_set_u8(TAIPAN_NS, "pool_enxsub2", fallback->extranonce_subscribe ? 1 : 0);
        if (err != BB_OK) return err;
        err = bb_nv_set_u8(TAIPAN_NS, "pool_dcdcb2",  fallback->decode_coinbase      ? 1 : 0);
        if (err != BB_OK) return err;
    } else {
        // Clear fallback by writing empty strings and 0 port
        err = bb_nv_set_str(TAIPAN_NS, "pool_host_2", "");
        if (err != BB_OK) return err;

        err = bb_nv_set_u16(TAIPAN_NS, "pool_port_2", 0);
        if (err != BB_OK) return err;

        err = bb_nv_set_str(TAIPAN_NS, "wallet_addr_2", "");
        if (err != BB_OK) return err;

        err = bb_nv_set_str(TAIPAN_NS, "worker_2", "");
        if (err != BB_OK) return err;

        err = bb_nv_set_str(TAIPAN_NS, "pool_pass_2", "");
        if (err != BB_OK) return err;

        /* Reset fallback bools to their defaults so a re-add starts clean. */
        err = bb_nv_set_u8(TAIPAN_NS, "pool_enxsub2", 0);
        if (err != BB_OK) return err;
        err = bb_nv_set_u8(TAIPAN_NS, "pool_dcdcb2",  1);
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

bb_err_t taipan_config_set_pool(const char *pool_host, uint16_t pool_port,
                                      const char *wallet_addr, const char *worker_name,
                                      const char *pool_pass)
{
    // Build primary pool config
    taipan_pool_cfg_t primary = {0};
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
    taipan_pool_cfg_t current_fallback = s_config.pools[1];

    // Only pass fallback if it's configured; otherwise pass NULL to clear it
    taipan_pool_cfg_t *fallback_ptr = taipan_config_pool_configured(TAIPAN_POOL_FALLBACK) ? &current_fallback : NULL;

    return taipan_config_set_pools(&primary, fallback_ptr);
}

bb_err_t taipan_config_set_hostname(const char *hostname)
{
    if (!valid_hostname(hostname)) {
        return BB_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    bb_err_t err = bb_nv_set_str(TAIPAN_NS, "hostname", hostname);
    if (err != BB_OK) {
        return err;
    }
#endif

    // Update in-memory cache on success
    strncpy(s_config.hostname, hostname, sizeof(s_config.hostname) - 1);
    s_config.hostname[sizeof(s_config.hostname) - 1] = '\0';

    return BB_OK;
}

bb_err_t taipan_config_register_manifest(void)
{
    static const bb_manifest_nv_t taipan_nv_keys[] = {
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

    return bb_manifest_register_nv(TAIPAN_NS, taipan_nv_keys,
                                   sizeof(taipan_nv_keys) / sizeof(taipan_nv_keys[0]));
}
