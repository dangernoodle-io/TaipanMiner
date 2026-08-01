#pragma once

#include <stdbool.h>
#include <stdint.h>

// Full share payload captured at mining.submit time. This is tm_stratum's
// OWN plain-POD record type, not a delivery/recording type: tm_stratum
// never depends on tm_pool_stats or any other sink -- a later PR's
// composition root maps this record onto whatever it records shares into
// (see stratum_fsm.h's on-accepted-share hook).
//
// Storage: a stratum_reqid_slot_t (stratum_reqid.h) carries one of these
// directly, valid only when the slot's kind is STRATUM_REQID_SUBMIT -- there
// is deliberately no separate id-keyed table for share records. An earlier
// revision of this component DID split them into two same-capacity,
// independently-evicted tables (this one keyed identically to
// stratum_reqid's own); under keepalive/extranonce_subscribe churn the
// shared reqid table could evict a live SUBMIT id before its response
// arrived while the (separately-evicted) share table still held the
// now-orphaned record, silently dropping an accepted share's hook
// invocation even though the accept counter still incremented (TA-571
// firmware review finding #2). Folding the payload into stratum_reqid's own
// slot makes that class of desync structurally impossible: an id and its
// share record now live and die in exactly the same eviction operation.
//
// version_bits/version_rolled split out of mining_result_t's single
// version_hex string: version_rolled is false (version_bits 0) when the
// share used no BIP 320 version rolling, matching the empty-string
// convention package_result() already uses for version_hex.
typedef struct {
    char     job_id[64];
    char     extranonce2_hex[17];
    uint8_t  extranonce2_len;   // strlen(extranonce2_hex) at capture time
    char     ntime_hex[9];
    char     nonce_hex[9];
    uint32_t version_bits;      // rolled bits actually submitted; 0 if not rolled
    bool     version_rolled;
    double   diff;              // pool-assigned difficulty at submit time
    // hash_out[24..31], internal little-endian order (see
    // mining_result_t.hash_prefix's doc comment) -- not reversed.
    uint8_t  hash_prefix[8];
} stratum_accepted_share_t;
