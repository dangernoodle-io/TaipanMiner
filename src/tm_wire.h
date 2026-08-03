#pragma once
// TM-local composition-root helpers, called by `bbtool codegen`-generated
// bb_app_init() -- and TaipanMiner v2's `bbtool codegen
// --consumer-manifest` file (see .breadboard/scripts/bbtool/README.md's
// "`args=` (parameterized init calls) and `--consumer-manifest`" section,
// and breadboard's own examples/smoke/main/bb_wire.h for the reference
// shape). This header is BOTH the declaration home for the functions below
// AND their consumer manifest: it carries `// bbtool:init` markers ONLY
// for TM-local composition calls that have no natural home in a vendored
// bb component header.
//
// Neither call fits a vendored bb component header's own
// `// bbtool:init` marker: bb_log_level_set()/bb_log_tag_register() are
// app-policy calls (which tags TM cares about, at what level), not a
// component's own composition-time entry point, and log_reset_reason()
// (renamed tm_log_reset_reason_init() here) is app-specific reboot-reason
// reporting. Both are wrapped as bb_err_t-returning, zero-arg functions so
// they fit the standard `// bbtool:init tier=early fn=<name>` call
// convention (see scripts/bbtool/commands/wire.py's module docstring) --
// codegen's marker grammar has no support for void-returning, unparameterized
// composition-root calls.
//
// The bb component EARLY-tier calls (bb_log_stream_init, bb_log_config_init,
// bb_storage_nvs_register, bb_wifi_ensure_net_stack, bb_wifi_autoinit,
// bb_system_boot_banner_init) already carry their own `// bbtool:init`
// markers in bb_log.h/bb_storage_nvs.h/bb_wifi.h/bb_system.h -- codegen
// discovers those via this project's bbtool.toml `[capability.tm_floor]`
// component closure; this file must NOT duplicate them.
//
// Neither of the tm_log_* manifest entries below has a real
// `requires=`/`provides=` edge into the component graph, so both tie-break
// to AFTER every early-tier component entry (manifest entries always
// parse-order after every collect_entries() component entry -- see
// wire_graph.py's topo_sort docstring). See tm_log_noise_suppress_init()'s
// doc comment below for the one behavior-visible consequence (log-noise
// suppression now applies after bb_wifi_autoinit() starts connecting, not
// before). The tm_pool_* manifest entries further below tie-break the same
// way, after the tm_log_* entries (parse order).
#include "bb_core.h"

// tm_pool_config_init()/tm_pool_stats_init()/tm_pool_policy_init() are real
// component functions (declared in their own components' public headers) --
// unlike tm_log_noise_suppress_init()/tm_log_reset_reason_init() above,
// they are NOT implemented in tm_wire.c and must not be redeclared here;
// these #includes are the only reason this header needs them (codegen's
// generated bb_app_init.c only #includes the manifest source file itself --
// see wire.py's `_headers_for` -- never a `component=`-named component's
// own header), so tm_wire.h pulls the real prototypes in on their behalf.
#include "tm_pool_cfg.h"
#include "tm_pool_stats.h"
#include "tm_pool_policy.h"
#include "tm_compose.h"

#ifdef __cplusplus
extern "C" {
#endif

// Suppresses noisy ESP-IDF WiFi/net framework log tags and registers TM's
// own "taipanminer" tag at INFO. Always returns BB_OK.
bb_err_t tm_log_noise_suppress_init(void);

// Logs the boot reset reason (WARN if abnormal). Always returns BB_OK.
bb_err_t tm_log_reset_reason_init(void);

// Runs mining_run_self_tests() (SW KAT, HW KAT, HW-vs-SW lockstep -- see
// mining.h) synchronously at boot, gating mining on any failure via
// mining_set_sha_self_test_failed(). Wrapped as a bb_err_t(*)(void) (like
// tm_log_reset_reason_init() above) purely for the manifest's call
// convention -- mining_run_self_tests() itself returns void and always
// "succeeds" from codegen's point of view; a FAILED self-test is reported
// via mining_sha_self_test_failed() (checked by mining_task()'s own entry
// gate at mining.c:739), not via this wrapper's return value. `early` tier
// (not `regular`) is load-bearing: tm_compose_mining_stratum_init() (the
// call that spawns the mining task) is `regular`-tier, so this always runs
// first regardless of parse order within `early`.
bb_err_t tm_mining_self_test_init(void);

#ifdef __cplusplus
}
#endif

// bbtool:init tier=early fn=tm_log_noise_suppress_init

// bbtool:init tier=early fn=tm_log_reset_reason_init

// bbtool:init tier=early fn=tm_mining_self_test_init component=tm_mining

// tm_pool_{config,stats,policy}_init() (TA-573 PR6) each have no natural
// component-header home: they are TM's own bring-up sequencing decision
// (config before stats before policy -- parse-order only; see below), not
// a fact any one of the three components can state about itself, so they
// live here as manifest entries with `component=` (B1-1275) rather than as
// `// bbtool:init` markers inside tm_pool_cfg.h/tm_pool_stats.h/
// tm_pool_policy.h. `component=` folds each named component's own
// REQUIRES/PRIV_REQUIRES closure into this board's build (bb_config,
// bb_storage_nvs, bb_timer, tm_pool_client, ...) -- without it these three
// components stay gc-stripped from the image exactly as before this PR.
// All three are already bb_err_t(*)(void) -- no wrapper needed.
//
// Only tm_pool_policy_init() carries `requires=storage_nvs`:
// tm_pool_policy_init() reads bb_config (-> bb_storage's "nvs" backend) at
// init via s_load_cfg(), so it must be ordered after
// bb_storage_nvs_register()'s provides=storage_nvs (same graph edge
// bb_wifi_autoinit()/bb_diag_panic_init() already declare -- see
// bb_wifi.h/bb_diag.h) -- correct-by-construction instead of relying on
// parse-order + has_default masking a real ordering dependency.
// tm_pool_config_init() (no-op) and tm_pool_stats_init() (RAM-cache reset +
// flush-timer arm only; no storage read at init) have no such edge, so they
// stay parse-order-only -- there is no genuine inter-component init
// dependency among the three (tm_pool_policy_init() reads bb_config
// directly, never tm_pool_config's own init state).

// bbtool:init tier=early fn=tm_pool_config_init component=tm_pool_config

// bbtool:init tier=early fn=tm_pool_stats_init component=tm_pool_stats

// bbtool:init tier=early fn=tm_pool_policy_init component=tm_pool_policy requires=storage_nvs

// tm_compose_mining_stratum_init() (TA-562) composes the mining + stratum
// tasks and the bb_bqueue work/result queues once the pool subsystem
// (config/stats/policy, above) has already brought up its own state --
// it reads tm_pool_policy_active_idx()/tm_pool_config_is_configured() at
// init, so it must run AFTER tm_pool_policy_init(). No natural
// component-header home (same rationale as the tm_pool_* entries above):
// this is TM's own bring-up sequencing decision, not a fact
// tm_compose can state about itself. `regular` tier (not
// `early`) -- it spawns FreeRTOS tasks, which is deferred past `early`'s
// boot-banner/reset-reason/pool-subsystem bring-up the same way every
// other task-spawning composition call is.

// bbtool:init tier=regular fn=tm_compose_mining_stratum_init component=tm_compose requires=storage_nvs
