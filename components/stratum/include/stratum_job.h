#pragma once

// Pool-side job data, parsed from a stratum mining.notify. Trimmed subset of
// the pre-rebuild `work.h` (see TaipanMiner jae/ta532-ota-hosts-guard
// components/mining/include/work.h): TA-560 owns the protocol FSM + pool
// message parsing only. Coinbase/merkle/header building (mining_work_t,
// build_coinbase_hash, etc.) is mining's job and does not exist in the v2
// floor yet -- that lands in a follow-up mining/work component port. Until
// then, a received job is handed across the stratum_work_publish() seam as
// this raw stratum_job_t; the mining side turns it into hashable work.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define STRATUM_MAX_COINB1_SIZE     256
#define STRATUM_MAX_COINB2_SIZE     256
#define STRATUM_MAX_EXTRANONCE1_SIZE 8
#define STRATUM_MAX_EXTRANONCE2_SIZE 8
#define STRATUM_MAX_MERKLE_BRANCHES  16

typedef struct {
    char     job_id[64];
    uint8_t  prevhash[32];       // raw prevhash (after endian fix)
    uint8_t  coinb1[STRATUM_MAX_COINB1_SIZE];
    size_t   coinb1_len;
    uint8_t  coinb2[STRATUM_MAX_COINB2_SIZE];
    size_t   coinb2_len;
    uint8_t  merkle_branches[STRATUM_MAX_MERKLE_BRANCHES][32];
    size_t   merkle_count;
    uint32_t version;
    uint32_t nbits;
    uint32_t ntime;
    bool     clean_jobs;
} stratum_job_t;
