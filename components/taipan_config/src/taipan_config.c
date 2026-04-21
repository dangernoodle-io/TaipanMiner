#include "taipan_config.h"
#include <string.h>
#include "bb_nv.h"
#include "bb_log.h"

#define TAIPAN_NS "taipanminer"

static struct {
    char pool_host[64];
    uint16_t pool_port;
    char wallet_addr[64];
    char worker_name[32];
    char pool_pass[64];
} s_config;

static const char *TAG = "taipan_config";

taipan_err_t taipan_config_init(void)
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

    // Load pool_port (u16 requires custom read since breadboard only has u8/u32)
    // Note: original implementation stored it as u16 directly in NVS
    // We need to read it via u32 fallback or implement a custom getter
    uint32_t port_u32 = 0;
    err = bb_nv_get_u32(TAIPAN_NS, "pool_port", &port_u32, 0);
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to load pool_port");
        return err;
    }
    s_config.pool_port = (uint16_t)port_u32;

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

#ifdef ESP_PLATFORM
taipan_err_t taipan_config_set_pool(const char *pool_host, uint16_t pool_port,
                                      const char *wallet_addr, const char *worker_name,
                                      const char *pool_pass)
{
    bb_err_t err;

    err = bb_nv_set_str(TAIPAN_NS, "pool_host", pool_host);
    if (err != BB_OK) return err;

    err = bb_nv_set_u32(TAIPAN_NS, "pool_port", (uint32_t)pool_port);
    if (err != BB_OK) return err;

    err = bb_nv_set_str(TAIPAN_NS, "wallet_addr", wallet_addr);
    if (err != BB_OK) return err;

    err = bb_nv_set_str(TAIPAN_NS, "worker", worker_name);
    if (err != BB_OK) return err;

    err = bb_nv_set_str(TAIPAN_NS, "pool_pass", pool_pass);
    if (err != BB_OK) return err;

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
#endif
