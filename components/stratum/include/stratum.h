#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "bb_json.h"
#include "bb_nv.h"
#include "work.h"

// Stratum v1 client task — runs on Core 0, priority 5
void stratum_task(void *arg);

// Get stratum connection status
bool stratum_is_connected(void);

// Request stratum reconnect (called from WiFi event handler on IP loss)
void stratum_request_reconnect(void);

// WiFi kick callback — called after consecutive connect failures to force reassociation
typedef void (*stratum_wifi_kick_cb_t)(void);
void stratum_set_wifi_kick_cb(stratum_wifi_kick_cb_t cb);

// Diagnostic getters — lock-free reads from interrupt-safe statics
uint32_t stratum_get_reconnect_delay_ms(void);
int stratum_get_connect_fail_count(void);

// Session start tick converted to milliseconds; 0 if not connected.
// Caller computes elapsed = pdTICKS_TO_MS(xTaskGetTickCount()) - this.
// Note: 32-bit ms wraparound at ~49.7 days; difference math is wrap-safe
// for sessions shorter than that.
uint32_t stratum_get_session_start_ms(void);

// Current pool difficulty (default 512.0 pre-connect; updated on
// mining.set_difficulty).
double stratum_get_difficulty(void);

// Snapshot of session-scoped negotiated values. Pointers in `extranonce1`
// remain valid until the next reconnect (s_state lives for the firmware
// lifetime; the reconnect path zeroes extranonce1_len). Returns false
// when not connected; *out is unmodified in that case.
typedef struct {
    const uint8_t *extranonce1;
    size_t         extranonce1_len;
    int            extranonce2_size;
    uint32_t       version_mask;  // 0 if version-rolling not negotiated
} stratum_session_snapshot_t;
bool stratum_get_session_snapshot(stratum_session_snapshot_t *out);

// Pointer to the most recent mining.notify job, or false if none yet
// (no connection or no notify since last reconnect). The returned pointer
// is valid until the next mining.notify or reconnect.
bool stratum_get_job_snapshot(const stratum_job_t **out);

// Get pool round-trip time (ms, EMA-smoothed). Returns -1 if no sample yet (TA-118).
int stratum_get_pool_rtt_ms(void);

// Parse stratum error code from JSON-RPC error item.
// Handles both [code, "message", "data"] array and {"code": N, ...} object forms.
// Returns the error code, or -1 if absent/unparseable.
int stratum_parse_error_code(bb_json_t error_item);

// Get active pool index: 0 = primary, 1 = fallback. Returns -1 if not connected.
int stratum_get_active_pool_idx(void);

// TA-306: Status of mining.extranonce.subscribe for the active session.
// Resets to OFF on each reconnect, then becomes PENDING/ACTIVE/REJECTED if
// the active slot's user pref enables it.
typedef enum {
    STRATUM_EXNX_SUB_OFF      = 0,
    STRATUM_EXNX_SUB_PENDING  = 1,
    STRATUM_EXNX_SUB_ACTIVE   = 2,
    STRATUM_EXNX_SUB_REJECTED = 3,
} stratum_extranonce_sub_status_t;
stratum_extranonce_sub_status_t stratum_get_extranonce_subscribe_status(void);

// Manually switch to idx and trigger reconnect. Returns BB_ERR_INVALID_ARG
// if idx out of range or that pool slot isn't configured.
bb_err_t stratum_request_switch_pool(int idx);
