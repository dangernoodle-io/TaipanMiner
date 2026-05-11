#pragma once
/*
 * DPORT-bus SHA-256 hardware backend (classic ESP32 only).
 *
 * Classic ESP32 SHA peripheral lives in DPORT space. Reads must be serialized
 * via DPORT_INTERRUPT_DISABLE() + DPORT_SEQUENCE_REG_READ() per Espressif
 * erratum, or the AHB bus may return garbage / panic the CPU.
 *
 * Ported verbatim from NerdMiner_v2/src/mining.cpp:903-1124 (TA-271 step B).
 */

#ifdef ESP_PLATFORM

#if CONFIG_IDF_TARGET_ESP32

#include <stdint.h>
#include <stdbool.h>
#include "bb_core.h"

void sha256_hw_dport_init(void);
void sha256_hw_dport_acquire(void);
void sha256_hw_dport_release(void);

/* Known-vector self-test: SHA-256("abc")
 * Returns BB_OK on PASS, BB_ERR_INVALID_STATE on FAIL */
bb_err_t sha256_hw_dport_self_test(void);

/* SW-vs-HW lockstep self-test: N nonces of sha256d(header) vs
 * sha256_hw_dport_per_nonce(). Logs byte divergence on mismatch.
 * Caller MUST hold sha256_hw_dport_acquire().
 * Returns BB_OK on PASS, BB_ERR_INVALID_STATE on mismatch. */
bb_err_t sha256_hw_dport_self_test_lockstep(void);

/* Boot probes bundle (calls lockstep self-test + logs result).
 * Caller MUST hold sha256_hw_dport_acquire(). */
void sha256_hw_dport_boot_probes(void);

/* Pool-target-aware early-reject on digest MSB word (state[7]).
 * Returns true on potential hit (MSB word <= target_word0_max). Caller
 * proceeds to full target check. Returns false on early reject.
 * hash_out is written only on true. */
bool sha256_hw_dport_per_nonce(const uint8_t header_80[80], uint32_t nonce, uint32_t target_word0_max, uint8_t hash_out[32]);

/* Compatibility aliases so mining.c can call sha256_hw_acquire/release
 * uniformly across targets without per-call target guards. */
static inline void sha256_hw_acquire(void) { sha256_hw_dport_acquire(); }
static inline void sha256_hw_release(void) { sha256_hw_dport_release(); }

/* TA-271 step B will define the per-nonce hot-loop API here. The signature
 * differs from the AHB version because classic ESP32 cannot inject midstate
 * (no writable H regs) — must re-hash block1 every nonce. */

#endif // CONFIG_IDF_TARGET_ESP32

#endif // ESP_PLATFORM
