#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ASIC_NONCE_DEDUP_SIZE 16

typedef struct {
    uint8_t  job_id;
    uint32_t nonce;
    uint32_t ver;
} asic_nonce_dedup_entry_t;

typedef struct {
    asic_nonce_dedup_entry_t entries[ASIC_NONCE_DEDUP_SIZE];
    uint8_t                  next_idx;  // ring write index
} asic_nonce_dedup_t;

// Reset state — call on clean-job (every miner job) to invalidate prior nonces.
void asic_nonce_dedup_reset(asic_nonce_dedup_t *d);

// Lookup-and-insert. Returns true if (job_id, nonce, ver) was already in the
// ring (caller should skip the share). Returns false if it was a new combo;
// in that case it has been inserted into the ring at the current write
// position (oldest evicted on wraparound).
bool asic_nonce_dedup_check_and_insert(asic_nonce_dedup_t *d,
                                        uint8_t job_id,
                                        uint32_t nonce,
                                        uint32_t ver);
