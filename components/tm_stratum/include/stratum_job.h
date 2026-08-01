#pragma once

// Pool-side job data, parsed from a stratum mining.notify. Internal staging
// only -- this type never crosses the tm_stratum component boundary (see
// stratum_work_seam.h, which carries a ready-to-hash mining_work_t instead).
// A received job is staged here by stratum_machine_handle_notify(), then
// build_work() (work_build.h) turns it into a mining_work_t.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define STRATUM_MAX_COINB1_SIZE      256
#define STRATUM_MAX_COINB2_SIZE      256
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
