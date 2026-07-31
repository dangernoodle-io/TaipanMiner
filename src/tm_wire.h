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
// Neither manifest entry below has a real `requires=`/`provides=` edge into
// the component graph, so both tie-break to AFTER every early-tier
// component entry (manifest entries always parse-order after every
// collect_entries() component entry -- see wire_graph.py's topo_sort
// docstring). See tm_log_noise_suppress_init()'s doc comment below for the
// one behavior-visible consequence (log-noise suppression now applies
// after bb_wifi_autoinit() starts connecting, not before).
#include "bb_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Suppresses noisy ESP-IDF WiFi/net framework log tags and registers TM's
// own "taipanminer" tag at INFO. Always returns BB_OK.
bb_err_t tm_log_noise_suppress_init(void);

// Logs the boot reset reason (WARN if abnormal). Always returns BB_OK.
bb_err_t tm_log_reset_reason_init(void);

#ifdef __cplusplus
}
#endif

// bbtool:init tier=early fn=tm_log_noise_suppress_init

// bbtool:init tier=early fn=tm_log_reset_reason_init
