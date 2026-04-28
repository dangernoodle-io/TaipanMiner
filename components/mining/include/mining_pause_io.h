#pragma once
#include <stdint.h>
#include <stdbool.h>

// Sync primitives for the pause/resume protocol. All take-with-timeout
// functions return true on acquire, false on timeout. Mutex/binary-sem
// distinction is preserved because their semantics differ (mutex is
// reentrancy-safe per-task; binary sem is fire-and-forget signal).
typedef struct {
    bool (*mutex_take)(uint32_t timeout_ms);
    void (*mutex_give)(void);
    bool (*ack_take)(uint32_t timeout_ms);
    void (*ack_give)(void);
    bool (*done_take)(uint32_t timeout_ms);
    void (*done_give)(void);
} mining_pause_sync_ops_t;

extern const mining_pause_sync_ops_t g_mining_pause_sync_ops_default;
