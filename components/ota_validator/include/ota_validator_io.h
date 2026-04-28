#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef void (*ota_timer_cb_t)(void *user);

typedef struct {
    bool (*create)(ota_timer_cb_t cb, void *user, void **out_handle);
    bool (*start_once)(void *handle, uint64_t timeout_us);
    void (*stop)(void *handle);
    void (*delete_)(void *handle);  // 'delete' is C++-reserved-ish; underscore-suffix avoids conflicts
} ota_timer_ops_t;

typedef struct {
    bool (*is_pending)(void);
    void (*mark_valid)(const char *reason);
} ota_mark_valid_ops_t;

// Wire production ops at boot. Call BEFORE the first ota_validator_on_* entry point.
void ota_validator_init(const ota_timer_ops_t *t, const ota_mark_valid_ops_t *m);

// Production-default op tables -- defined in ota_validator_espidf.c.
extern const ota_timer_ops_t      g_ota_timer_ops_default;
extern const ota_mark_valid_ops_t g_ota_mark_valid_ops_default;
