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
    char pool_host[64];
    uint16_t pool_port;
    char wallet_addr[64];
    char worker_name[32];
    char pool_pass[64];
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

    // Load pool_host
    err = bb_nv_get_str(TAIPAN_NS, "pool_host", s_config.pool_host, sizeof(s_config.pool_host), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load pool_host");
        return err;
    }

    // Load wallet_addr
    err = bb_nv_get_str(TAIPAN_NS, "wallet_addr", s_config.wallet_addr, sizeof(s_config.wallet_addr), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load wallet_addr");
        return err;
    }

    // Load worker_name
    err = bb_nv_get_str(TAIPAN_NS, "worker", s_config.worker_name, sizeof(s_config.worker_name), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load worker_name");
        return err;
    }

    // Load pool_pass
    err = bb_nv_get_str(TAIPAN_NS, "pool_pass", s_config.pool_pass, sizeof(s_config.pool_pass), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load pool_pass");
        return err;
    }

    err = bb_nv_get_u16(TAIPAN_NS, "pool_port", &s_config.pool_port, 0);
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load pool_port");
        return err;
    }

    // Load hostname
    err = bb_nv_get_str(TAIPAN_NS, "hostname", s_config.hostname, sizeof(s_config.hostname), "");
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load hostname");
        return err;
    }

#ifdef ESP_PLATFORM
    // Migration: if hostname is empty and worker is set, derive hostname from worker
    if (s_config.hostname[0] == '\0' && s_config.worker_name[0] != '\0') {
        char normalized[64];
        bb_mdns_build_hostname(s_config.worker_name, NULL, normalized, sizeof(normalized));
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
             s_config.pool_host, s_config.pool_port,
             s_config.wallet_addr, s_config.worker_name);
    return 0;
#else
    memset(&s_config, 0, sizeof(s_config));
    return 0;
#endif
}

const char *taipan_config_pool_host(void) { return s_config.pool_host; }
uint16_t taipan_config_pool_port(void) { return s_config.pool_port; }
const char *taipan_config_wallet_addr(void) { return s_config.wallet_addr; }
const char *taipan_config_worker_name(void) { return s_config.worker_name; }
const char *taipan_config_pool_pass(void) { return s_config.pool_pass; }
const char *taipan_config_hostname(void) { return s_config.hostname; }

bb_err_t taipan_config_set_pool(const char *pool_host, uint16_t pool_port,
                                      const char *wallet_addr, const char *worker_name,
                                      const char *pool_pass)
{
#ifdef ESP_PLATFORM
    bb_err_t err;

    err = bb_nv_set_str(TAIPAN_NS, "pool_host", pool_host);
    if (err != BB_OK) return err;

    err = bb_nv_set_u16(TAIPAN_NS, "pool_port", pool_port);
    if (err != BB_OK) return err;

    err = bb_nv_set_str(TAIPAN_NS, "wallet_addr", wallet_addr);
    if (err != BB_OK) return err;

    err = bb_nv_set_str(TAIPAN_NS, "worker", worker_name);
    if (err != BB_OK) return err;

    err = bb_nv_set_str(TAIPAN_NS, "pool_pass", pool_pass);
    if (err != BB_OK) return err;
#endif

    // Update in-memory cache on success
    strncpy(s_config.pool_host, pool_host, sizeof(s_config.pool_host) - 1);
    s_config.pool_host[sizeof(s_config.pool_host) - 1] = '\0';
    s_config.pool_port = pool_port;
    strncpy(s_config.wallet_addr, wallet_addr, sizeof(s_config.wallet_addr) - 1);
    s_config.wallet_addr[sizeof(s_config.wallet_addr) - 1] = '\0';
    strncpy(s_config.worker_name, worker_name, sizeof(s_config.worker_name) - 1);
    s_config.worker_name[sizeof(s_config.worker_name) - 1] = '\0';
    strncpy(s_config.pool_pass, pool_pass, sizeof(s_config.pool_pass) - 1);
    s_config.pool_pass[sizeof(s_config.pool_pass) - 1] = '\0';

    return 0;
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
    };

    return bb_manifest_register_nv(TAIPAN_NS, taipan_nv_keys,
                                   sizeof(taipan_nv_keys) / sizeof(taipan_nv_keys[0]));
}
