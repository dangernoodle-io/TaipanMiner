/*
 * routes_json.c — pure JSON-builder functions for TaipanMiner-owned GET routes.
 *
 * No ESP-IDF includes, no I/O, no mutexes — host-compilable.
 * Builders write into a caller-provided bb_json_t root.
 */

#include "routes_json.h"
#include "bb_json.h"
#include "bb_http.h"
#include "bb_core.h"
#include "work.h"       /* bytes_to_hex */
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * EMIT_NULLABLE macro — sentinel-null for double/float fields (>= 0.0).
 * ========================================================================= */

#define EMIT_NULLABLE(field, name) \
    do { \
        if ((s->field) >= 0.0) bb_json_obj_set_number(root, name, (double)(s->field)); \
        else                   bb_json_obj_set_null  (root, name); \
    } while (0)

/* ============================================================================
 * /api/stats
 * ========================================================================= */

void emit_stats_json(bb_http_json_obj_stream_t *obj, const stats_snapshot_t *snap)
{
    int64_t uptime_s = (snap->session_start_us > 0)
                       ? (snap->now_us - snap->session_start_us) / 1000000
                       : 0;
    int64_t last_share_ago_s = (snap->last_share_us > 0)
                               ? (snap->now_us - snap->last_share_us) / 1000000
                               : -1;

    bb_http_resp_json_obj_set_num(obj, "hashrate",        snap->hw_rate);
    bb_http_resp_json_obj_set_num(obj, "hashrate_avg",    snap->hw_ema);
    bb_http_resp_json_obj_set_num(obj, "temp_c",          (double)snap->temp_c);
    bb_http_resp_json_obj_set_int(obj, "shares",          (int64_t)snap->hw_shares);
    bb_http_resp_json_obj_set_int(obj, "session_shares",  (int64_t)snap->session_shares);
    bb_http_resp_json_obj_set_int(obj, "session_rejected",(int64_t)snap->session_rejected);
    bb_http_resp_json_obj_set_int(obj, "session_blocks_found", (int64_t)snap->session_blocks_found);
    bb_http_resp_json_obj_set_int(obj, "session_best_diff_ts", snap->session_best_diff_ts);
    bb_http_resp_json_obj_set_int(obj, "session_last_block_ts", snap->session_last_block_ts);

    bb_http_resp_json_obj_set_obj_begin(obj, "rejected");
    bb_http_resp_json_obj_set_int(obj, "total",           (int64_t)snap->session_rejected);
    bb_http_resp_json_obj_set_int(obj, "job_not_found",   (int64_t)snap->session_rejected_job_not_found);
    bb_http_resp_json_obj_set_int(obj, "low_difficulty",  (int64_t)snap->session_rejected_low_difficulty);
    bb_http_resp_json_obj_set_int(obj, "duplicate",       (int64_t)snap->session_rejected_duplicate);
    bb_http_resp_json_obj_set_int(obj, "stale_prevhash",  (int64_t)snap->session_rejected_stale_prevhash);
    bb_http_resp_json_obj_set_int(obj, "other",           (int64_t)snap->session_rejected_other);
    bb_http_resp_json_obj_set_int(obj, "other_last_code", (int64_t)snap->session_rejected_other_last_code);
    bb_http_resp_json_obj_set_obj_end(obj);

    bb_http_resp_json_obj_set_int(obj, "last_share_ago_s", last_share_ago_s);
    bb_http_resp_json_obj_set_num(obj, "best_diff",        snap->best_diff);
    bb_http_resp_json_obj_set_int(obj, "uptime_s",         uptime_s);

    if (snap->expected_ghs >= 0.0) {
        bb_http_resp_json_obj_set_num(obj, "expected_ghs", snap->expected_ghs);
    } else {
        bb_http_resp_json_obj_set_null(obj, "expected_ghs");
    }

#ifndef ASIC_CHIP
    if (snap->hashrate_1m >= 0.0)             bb_http_resp_json_obj_set_num(obj, "hashrate_1m",             snap->hashrate_1m);
    else                                      bb_http_resp_json_obj_set_null(obj, "hashrate_1m");
    if (snap->hashrate_10m >= 0.0)            bb_http_resp_json_obj_set_num(obj, "hashrate_10m",            snap->hashrate_10m);
    else                                      bb_http_resp_json_obj_set_null(obj, "hashrate_10m");
    if (snap->hashrate_1h >= 0.0)             bb_http_resp_json_obj_set_num(obj, "hashrate_1h",             snap->hashrate_1h);
    else                                      bb_http_resp_json_obj_set_null(obj, "hashrate_1h");
    if (snap->pool_effective_hashrate >= 0.0) bb_http_resp_json_obj_set_num(obj, "pool_effective_hashrate", snap->pool_effective_hashrate);
    else                                      bb_http_resp_json_obj_set_null(obj, "pool_effective_hashrate");
    if (snap->hw_error_pct_1m >= 0.0)         bb_http_resp_json_obj_set_num(obj, "hw_error_pct_1m",         snap->hw_error_pct_1m);
    else                                      bb_http_resp_json_obj_set_null(obj, "hw_error_pct_1m");
    if (snap->hw_error_pct_10m >= 0.0)        bb_http_resp_json_obj_set_num(obj, "hw_error_pct_10m",        snap->hw_error_pct_10m);
    else                                      bb_http_resp_json_obj_set_null(obj, "hw_error_pct_10m");
    if (snap->hw_error_pct_1h >= 0.0)         bb_http_resp_json_obj_set_num(obj, "hw_error_pct_1h",         snap->hw_error_pct_1h);
    else                                      bb_http_resp_json_obj_set_null(obj, "hw_error_pct_1h");
#endif

#ifdef ASIC_CHIP
    bb_http_resp_json_obj_set_num(obj, "asic_hashrate",     snap->asic_rate);
    bb_http_resp_json_obj_set_num(obj, "asic_hashrate_avg", snap->asic_ema);
    bb_http_resp_json_obj_set_int(obj, "asic_shares",       (int64_t)snap->asic_shares);
    bb_http_resp_json_obj_set_num(obj, "asic_temp_c",       (double)snap->asic_temp_c);
    if (snap->asic_freq_cfg >= 0.0f) bb_http_resp_json_obj_set_num(obj, "asic_freq_configured_mhz", (double)snap->asic_freq_cfg);
    else                             bb_http_resp_json_obj_set_null(obj, "asic_freq_configured_mhz");
    if (snap->asic_freq_eff >= 0.0f) bb_http_resp_json_obj_set_num(obj, "asic_freq_effective_mhz", (double)snap->asic_freq_eff);
    else                             bb_http_resp_json_obj_set_null(obj, "asic_freq_effective_mhz");
    bb_http_resp_json_obj_set_int(obj, "asic_small_cores", (int64_t)snap->asic_small_cores);
    bb_http_resp_json_obj_set_int(obj, "asic_count",       (int64_t)snap->asic_count);
    if (snap->asic_total_valid) {
        bb_http_resp_json_obj_set_num(obj, "asic_total_ghs",    (double)snap->asic_total_ghs);
        bb_http_resp_json_obj_set_num(obj, "asic_hw_error_pct", (double)snap->asic_hw_error_pct);
        if (snap->asic_total_ghs_1m >= 0.0f)   bb_http_resp_json_obj_set_num(obj, "asic_total_ghs_1m",   (double)snap->asic_total_ghs_1m);
        else                                    bb_http_resp_json_obj_set_null(obj, "asic_total_ghs_1m");
        if (snap->asic_total_ghs_10m >= 0.0f)  bb_http_resp_json_obj_set_num(obj, "asic_total_ghs_10m",  (double)snap->asic_total_ghs_10m);
        else                                    bb_http_resp_json_obj_set_null(obj, "asic_total_ghs_10m");
        if (snap->asic_total_ghs_1h >= 0.0f)   bb_http_resp_json_obj_set_num(obj, "asic_total_ghs_1h",   (double)snap->asic_total_ghs_1h);
        else                                    bb_http_resp_json_obj_set_null(obj, "asic_total_ghs_1h");
        if (snap->asic_hw_error_pct_1m >= 0.0f)  bb_http_resp_json_obj_set_num(obj, "asic_hw_error_pct_1m",  (double)snap->asic_hw_error_pct_1m);
        else                                      bb_http_resp_json_obj_set_null(obj, "asic_hw_error_pct_1m");
        if (snap->asic_hw_error_pct_10m >= 0.0f) bb_http_resp_json_obj_set_num(obj, "asic_hw_error_pct_10m", (double)snap->asic_hw_error_pct_10m);
        else                                      bb_http_resp_json_obj_set_null(obj, "asic_hw_error_pct_10m");
        if (snap->asic_hw_error_pct_1h >= 0.0f)  bb_http_resp_json_obj_set_num(obj, "asic_hw_error_pct_1h",  (double)snap->asic_hw_error_pct_1h);
        else                                      bb_http_resp_json_obj_set_null(obj, "asic_hw_error_pct_1h");
    } else {
        bb_http_resp_json_obj_set_null(obj, "asic_total_ghs");
        bb_http_resp_json_obj_set_null(obj, "asic_hw_error_pct");
        bb_http_resp_json_obj_set_null(obj, "asic_total_ghs_1m");
        bb_http_resp_json_obj_set_null(obj, "asic_total_ghs_10m");
        bb_http_resp_json_obj_set_null(obj, "asic_total_ghs_1h");
        bb_http_resp_json_obj_set_null(obj, "asic_hw_error_pct_1m");
        bb_http_resp_json_obj_set_null(obj, "asic_hw_error_pct_10m");
        bb_http_resp_json_obj_set_null(obj, "asic_hw_error_pct_1h");
    }
    if (snap->pool_effective_hashrate >= 0.0) bb_http_resp_json_obj_set_num(obj, "pool_effective_hashrate", snap->pool_effective_hashrate);
    else                                      bb_http_resp_json_obj_set_null(obj, "pool_effective_hashrate");

    bb_http_resp_json_obj_set_arr_begin(obj, "asic_chips");
    for (int c = 0; c < snap->n_chips; c++) {
        bb_http_resp_json_obj_set_obj_begin(obj, NULL);
        bb_http_resp_json_obj_set_int(obj, "idx",         (int64_t)c);
        bb_http_resp_json_obj_set_num(obj, "total_ghs",   (double)snap->chips[c].total_ghs);
        bb_http_resp_json_obj_set_num(obj, "error_ghs",   (double)snap->chips[c].error_ghs);
        bb_http_resp_json_obj_set_num(obj, "hw_err_pct",  (double)snap->chips[c].hw_err_pct);
        bb_http_resp_json_obj_set_int(obj, "total_raw",   (int64_t)snap->chips[c].total_raw);
        bb_http_resp_json_obj_set_int(obj, "error_raw",   (int64_t)snap->chips[c].error_raw);
        bb_http_resp_json_obj_set_num(obj, "total_drops", (double)snap->chips[c].total_drops);
        bb_http_resp_json_obj_set_num(obj, "error_drops", (double)snap->chips[c].error_drops);
        if (snap->chips[c].last_drop_us == 0 || snap->now_us < (int64_t)snap->chips[c].last_drop_us) {
            bb_http_resp_json_obj_set_null(obj, "last_drop_ago_s");
        } else {
            uint64_t ago_us = (uint64_t)snap->now_us - snap->chips[c].last_drop_us;
            bb_http_resp_json_obj_set_num(obj, "last_drop_ago_s", (double)(ago_us / 1000000ULL));
        }
        bb_http_resp_json_obj_set_arr_begin(obj, "domain_ghs");
        for (int d = 0; d < 4; d++) bb_http_resp_json_obj_set_num(obj, NULL, (double)snap->chips[c].domain_ghs[d]);
        bb_http_resp_json_obj_set_arr_end(obj);
        bb_http_resp_json_obj_set_arr_begin(obj, "domain_drops");
        for (int d = 0; d < 4; d++) bb_http_resp_json_obj_set_num(obj, NULL, (double)snap->chips[c].domain_drops[d]);
        bb_http_resp_json_obj_set_arr_end(obj);
        bb_http_resp_json_obj_set_obj_end(obj);
    }
    bb_http_resp_json_obj_set_arr_end(obj);
#endif
}

/* ============================================================================
 * /api/pool
 * ========================================================================= */

void emit_pool_json(bb_http_json_obj_stream_t *obj,
                    const pool_snapshot_t *s,
                    const pool_stat_snapshot_t *stats,
                    size_t stats_count)
{
    bb_http_resp_json_obj_set_str(obj, "host",   s->host);
    bb_http_resp_json_obj_set_int(obj, "port",   (int64_t)s->port);
    bb_http_resp_json_obj_set_str(obj, "worker", s->worker);
    bb_http_resp_json_obj_set_str(obj, "wallet", s->wallet);
    bb_http_resp_json_obj_set_bool(obj, "connected", s->connected);

    if (s->has_session_start) {
        bb_http_resp_json_obj_set_int(obj, "session_start_ago_s", (int64_t)s->session_start_ago_s);
    } else {
        bb_http_resp_json_obj_set_null(obj, "session_start_ago_s");
    }

    bb_http_resp_json_obj_set_num(obj, "current_difficulty", s->current_difficulty);

    if (s->pool_effective_hashrate >= 0.0) bb_http_resp_json_obj_set_num(obj, "pool_effective_hashrate", s->pool_effective_hashrate);
    else                                  bb_http_resp_json_obj_set_null(obj, "pool_effective_hashrate");
    if (s->pool_effective_hashrate_1m >= 0.0) bb_http_resp_json_obj_set_num(obj, "pool_effective_hashrate_1m", s->pool_effective_hashrate_1m);
    else                                     bb_http_resp_json_obj_set_null(obj, "pool_effective_hashrate_1m");
    if (s->pool_effective_hashrate_10m >= 0.0) bb_http_resp_json_obj_set_num(obj, "pool_effective_hashrate_10m", s->pool_effective_hashrate_10m);
    else                                      bb_http_resp_json_obj_set_null(obj, "pool_effective_hashrate_10m");
    if (s->pool_effective_hashrate_1h >= 0.0) bb_http_resp_json_obj_set_num(obj, "pool_effective_hashrate_1h", s->pool_effective_hashrate_1h);
    else                                     bb_http_resp_json_obj_set_null(obj, "pool_effective_hashrate_1h");

    if (s->latency_ms >= 0) bb_http_resp_json_obj_set_int(obj, "latency_ms", (int64_t)s->latency_ms);
    else                   bb_http_resp_json_obj_set_null(obj, "latency_ms");

    if (s->extranonce1_len > 0) {
        char en1_hex[2 * ROUTES_JSON_EXTRANONCE1_MAX + 1];
        bytes_to_hex(s->extranonce1, s->extranonce1_len, en1_hex);
        bb_http_resp_json_obj_set_str(obj, "extranonce1", en1_hex);
        bb_http_resp_json_obj_set_int(obj, "extranonce2_size", (int64_t)s->extranonce2_size);
        if (s->version_mask != 0) {
            char vm_hex[9];
            snprintf(vm_hex, sizeof(vm_hex), "%08lx", (unsigned long)s->version_mask);
            bb_http_resp_json_obj_set_str(obj, "version_mask", vm_hex);
        } else {
            bb_http_resp_json_obj_set_null(obj, "version_mask");
        }
    } else {
        bb_http_resp_json_obj_set_null(obj, "extranonce1");
        bb_http_resp_json_obj_set_null(obj, "extranonce2_size");
        bb_http_resp_json_obj_set_null(obj, "version_mask");
    }

    if (s->has_notify) {
        bb_http_resp_json_obj_set_obj_begin(obj, "notify");
        bb_http_resp_json_obj_set_str(obj, "job_id", s->job_id);

        char prevhash_hex[65];
        bytes_to_hex(s->prevhash, 32, prevhash_hex);
        bb_http_resp_json_obj_set_str(obj, "prev_hash", prevhash_hex);

        char coinb1_hex[2 * ROUTES_JSON_MAX_COINB + 1];
        bytes_to_hex(s->coinb1, s->coinb1_len, coinb1_hex);
        bb_http_resp_json_obj_set_str(obj, "coinb1", coinb1_hex);

        char coinb2_hex[2 * ROUTES_JSON_MAX_COINB + 1];
        bytes_to_hex(s->coinb2, s->coinb2_len, coinb2_hex);
        bb_http_resp_json_obj_set_str(obj, "coinb2", coinb2_hex);

        bb_http_resp_json_obj_set_arr_begin(obj, "merkle_branches");
        for (size_t i = 0; i < s->merkle_count; i++) {
            char br_hex[65];
            bytes_to_hex(s->merkle_branches[i], 32, br_hex);
            bb_http_resp_json_obj_set_str(obj, NULL, br_hex);
        }
        bb_http_resp_json_obj_set_arr_end(obj);

        char hex8[9];
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)s->version);
        bb_http_resp_json_obj_set_str(obj, "version", hex8);
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)s->nbits);
        bb_http_resp_json_obj_set_str(obj, "nbits", hex8);
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)s->ntime);
        bb_http_resp_json_obj_set_str(obj, "ntime", hex8);

        bb_http_resp_json_obj_set_bool(obj, "clean_jobs", s->clean_jobs);
        bb_http_resp_json_obj_set_obj_end(obj);
    } else {
        bb_http_resp_json_obj_set_null(obj, "notify");
    }

    if (s->active_pool_idx >= 0) bb_http_resp_json_obj_set_int(obj, "active_pool_idx", (int64_t)s->active_pool_idx);
    else                        bb_http_resp_json_obj_set_null(obj, "active_pool_idx");

    {
        const char *sub_str;
        switch (s->extranonce_subscribe_status) {
            case 1:  sub_str = "pending";  break;
            case 2:  sub_str = "active";   break;
            case 3:  sub_str = "rejected"; break;
            default: sub_str = "off";      break;
        }
        bb_http_resp_json_obj_set_str(obj, "extranonce_subscribe_status", sub_str);
    }

    bb_http_resp_json_obj_set_int(obj, "lifetime_blocks_total",  (int64_t)s->lifetime_blocks_total);
    bb_http_resp_json_obj_set_int(obj, "lifetime_last_block_ts", s->lifetime_last_block_ts);

    bb_http_resp_json_obj_set_obj_begin(obj, "configured");
    for (int i = 0; i < 2; i++) {
        const char *cfg_key = (i == 0) ? "primary" : "fallback";
        if (s->configured[i].configured) {
            bb_http_resp_json_obj_set_obj_begin(obj, cfg_key);
            bb_http_resp_json_obj_set_str(obj, "host",   s->configured[i].host);
            bb_http_resp_json_obj_set_int(obj, "port",   (int64_t)s->configured[i].port);
            bb_http_resp_json_obj_set_str(obj, "worker", s->configured[i].worker);
            bb_http_resp_json_obj_set_str(obj, "wallet", s->configured[i].wallet);
            bb_http_resp_json_obj_set_bool(obj, "extranonce_subscribe", s->configured[i].extranonce_subscribe);
            bb_http_resp_json_obj_set_bool(obj, "decode_coinbase",      s->configured[i].decode_coinbase);
            bb_http_resp_json_obj_set_obj_end(obj);
        } else {
            bb_http_resp_json_obj_set_null(obj, cfg_key);
        }
    }
    bb_http_resp_json_obj_set_obj_end(obj);

    /* per-pool stats array */
    bb_http_resp_json_obj_set_arr_begin(obj, "stats");
    for (size_t i = 0; i < stats_count; i++) {
        bb_http_resp_json_obj_set_obj_begin(obj, NULL);
        bb_http_resp_json_obj_set_str(obj, "host",         stats[i].host);
        bb_http_resp_json_obj_set_int(obj, "port",         (int64_t)stats[i].port);
        bb_http_resp_json_obj_set_int(obj, "shares",       (int64_t)stats[i].shares);
        bb_http_resp_json_obj_set_int(obj, "hashes",       (int64_t)stats[i].hashes);
        bb_http_resp_json_obj_set_num(obj, "best_diff",    stats[i].best_diff);
        bb_http_resp_json_obj_set_int(obj, "blocks_found", (int64_t)stats[i].blocks_found);
        bb_http_resp_json_obj_set_int(obj, "last_seen_s",  (int64_t)(stats[i].last_seen_us / 1000000));
        bb_http_resp_json_obj_set_int(obj, "best_diff_ts", stats[i].best_diff_ts);
        bb_http_resp_json_obj_set_int(obj, "last_block_ts", stats[i].last_block_ts);
        bb_http_resp_json_obj_set_obj_end(obj);
    }
    bb_http_resp_json_obj_set_arr_end(obj);
}

/* ============================================================================
 * /api/diag/asic
 * ========================================================================= */

void emit_diag_asic_json(bb_http_json_obj_stream_t *obj, const diag_asic_snapshot_t *s)
{
    bb_http_resp_json_obj_set_arr_begin(obj, "recent_drops");
    for (size_t i = 0; i < s->n_drops; i++) {
        const routes_json_drop_event_t *d = &s->drops[i];
        uint64_t age_us = (d->ts_us <= s->now_us) ? (s->now_us - d->ts_us) : 0;
        const char *kind_str;
        switch ((routes_json_drop_kind_t)d->kind) {
            case ROUTES_JSON_DROP_KIND_ERROR:  kind_str = "error";  break;
            case ROUTES_JSON_DROP_KIND_DOMAIN: kind_str = "domain"; break;
            default:                           kind_str = "total";  break;
        }
        bb_http_resp_json_obj_set_obj_begin(obj, NULL);
        bb_http_resp_json_obj_set_num(obj, "ts_ago_s",  (double)(age_us / 1000000ULL));
        bb_http_resp_json_obj_set_int(obj, "chip",      (int64_t)d->chip_idx);
        bb_http_resp_json_obj_set_str(obj, "kind",      kind_str);
        bb_http_resp_json_obj_set_int(obj, "domain",    (int64_t)d->domain_idx);
        bb_http_resp_json_obj_set_int(obj, "addr",      (int64_t)d->asic_addr);
        bb_http_resp_json_obj_set_num(obj, "ghs",       (double)d->ghs);
        bb_http_resp_json_obj_set_int(obj, "delta",     (int64_t)d->delta);
        bb_http_resp_json_obj_set_num(obj, "elapsed_s", (double)d->elapsed_s);
        bb_http_resp_json_obj_set_obj_end(obj);
    }
    bb_http_resp_json_obj_set_arr_end(obj);
}

/* ============================================================================
 * /api/knot
 * ========================================================================= */

bb_json_t build_knot_peer_json(const knot_peer_t *peer, int64_t now_us)
{
    bb_json_t peer_obj = bb_json_obj_new();
    bb_json_obj_set_string(peer_obj, "instance",  peer->instance_name);
    bb_json_obj_set_string(peer_obj, "hostname",  peer->hostname);
    bb_json_obj_set_string(peer_obj, "ip",        peer->ip4);
    bb_json_obj_set_string(peer_obj, "worker",    peer->worker);
    bb_json_obj_set_string(peer_obj, "board",     peer->board);
    bb_json_obj_set_string(peer_obj, "version",   peer->version);
    bb_json_obj_set_string(peer_obj, "state",     peer->state);
    bb_json_obj_set_bool(peer_obj, "ui",          peer->ui);

    int64_t seen_ago_s = (now_us - peer->last_seen_us) / 1000000;
    bb_json_obj_set_number(peer_obj, "seen_ago_s", (double)seen_ago_s);

    return peer_obj;
}

/* ============================================================================
 * /api/settings GET
 * ========================================================================= */

void emit_settings_json(bb_http_json_obj_stream_t *obj, const settings_snapshot_t *snap)
{
    bb_http_resp_json_obj_set_str(obj,  "hostname",       snap->hostname);
    bb_http_resp_json_obj_set_bool(obj, "display_en",     snap->display_en);
    bb_http_resp_json_obj_set_bool(obj, "ota_skip_check", snap->ota_skip_check);
    bb_http_resp_json_obj_set_bool(obj, "mdns_en",        snap->mdns_en);
    bb_http_resp_json_obj_set_bool(obj, "knot_en",        snap->knot_en);
    bb_http_resp_json_obj_set_bool(obj, "provisioned",    snap->provisioned);
}

/* ============================================================================
 * /api/power  (ASIC_CHIP only)
 * ========================================================================= */

#ifdef ASIC_CHIP
void emit_power_json(bb_http_json_obj_stream_t *obj, const power_snapshot_t *snap)
{
    if (snap->vcore_mv >= 0)  bb_http_resp_json_obj_set_int(obj, "vcore_mv", (int64_t)snap->vcore_mv);
    else                      bb_http_resp_json_obj_set_null(obj, "vcore_mv");
    if (snap->icore_ma >= 0)  bb_http_resp_json_obj_set_int(obj, "icore_ma", (int64_t)snap->icore_ma);
    else                      bb_http_resp_json_obj_set_null(obj, "icore_ma");
    if (snap->pcore_mw >= 0)  bb_http_resp_json_obj_set_int(obj, "pcore_mw", (int64_t)snap->pcore_mw);
    else                      bb_http_resp_json_obj_set_null(obj, "pcore_mw");
    {
        /* asic_hashrate is H/s; divide by 1e9 to get GH/s for mining_efficiency_jth */
        double eff = mining_efficiency_jth((double)snap->pcore_mw, snap->asic_hashrate / 1e9);
        if (eff >= 0.0) bb_http_resp_json_obj_set_num(obj, "efficiency_jth", eff);
        else            bb_http_resp_json_obj_set_null(obj, "efficiency_jth");
    }
    if (snap->efficiency_jth_1m >= 0.0)         bb_http_resp_json_obj_set_num(obj, "efficiency_jth_1m", snap->efficiency_jth_1m);
    else                                         bb_http_resp_json_obj_set_null(obj, "efficiency_jth_1m");
    if (snap->efficiency_jth_10m >= 0.0)        bb_http_resp_json_obj_set_num(obj, "efficiency_jth_10m", snap->efficiency_jth_10m);
    else                                         bb_http_resp_json_obj_set_null(obj, "efficiency_jth_10m");
    if (snap->efficiency_jth_1h >= 0.0)         bb_http_resp_json_obj_set_num(obj, "efficiency_jth_1h", snap->efficiency_jth_1h);
    else                                         bb_http_resp_json_obj_set_null(obj, "efficiency_jth_1h");
    if (snap->expected_efficiency_jth >= 0.0)   bb_http_resp_json_obj_set_num(obj, "expected_efficiency_jth", snap->expected_efficiency_jth);
    else                                         bb_http_resp_json_obj_set_null(obj, "expected_efficiency_jth");
    if (snap->vin_mv >= 0) {
        bb_http_resp_json_obj_set_int(obj, "vin_mv", (int64_t)snap->vin_mv);
        bool vin_low = (snap->vin_mv < (snap->nominal_vin_mv + 500) * 87 / 100);
        bb_http_resp_json_obj_set_bool(obj, "vin_low", vin_low);
    } else {
        bb_http_resp_json_obj_set_null(obj, "vin_mv");
        bb_http_resp_json_obj_set_null(obj, "vin_low");
    }
    if (snap->board_temp_c >= 0.0f) bb_http_resp_json_obj_set_num(obj, "board_temp_c", (double)snap->board_temp_c);
    else                            bb_http_resp_json_obj_set_null(obj, "board_temp_c");
    if (snap->vr_temp_c >= 0.0f)   bb_http_resp_json_obj_set_num(obj, "vr_temp_c", (double)snap->vr_temp_c);
    else                            bb_http_resp_json_obj_set_null(obj, "vr_temp_c");
}

/* ============================================================================
 * /api/fan  (ASIC_CHIP only)
 * ========================================================================= */

void emit_fan_json(bb_http_json_obj_stream_t *obj, const fan_snapshot_t *snap)
{
    if (snap->fan_rpm >= 0)      bb_http_resp_json_obj_set_int(obj, "rpm",       (int64_t)snap->fan_rpm);
    else                         bb_http_resp_json_obj_set_null(obj, "rpm");
    if (snap->fan_duty_pct >= 0) bb_http_resp_json_obj_set_int(obj, "duty_pct",  (int64_t)snap->fan_duty_pct);
    else                         bb_http_resp_json_obj_set_null(obj, "duty_pct");
    bb_http_resp_json_obj_set_bool(obj, "autofan", snap->autofan);
    if (snap->die_target_c >= 0) bb_http_resp_json_obj_set_int(obj, "die_target_c", (int64_t)snap->die_target_c);
    else                         bb_http_resp_json_obj_set_null(obj, "die_target_c");
    if (snap->vr_target_c >= 0)  bb_http_resp_json_obj_set_int(obj, "vr_target_c",  (int64_t)snap->vr_target_c);
    else                         bb_http_resp_json_obj_set_null(obj, "vr_target_c");
    if (snap->manual_pct >= 0)   bb_http_resp_json_obj_set_int(obj, "manual_pct",   (int64_t)snap->manual_pct);
    else                         bb_http_resp_json_obj_set_null(obj, "manual_pct");
    if (snap->min_pct >= 0)      bb_http_resp_json_obj_set_int(obj, "min_pct",       (int64_t)snap->min_pct);
    else                         bb_http_resp_json_obj_set_null(obj, "min_pct");
    if (snap->die_ema_c >= 0.0f) bb_http_resp_json_obj_set_num(obj, "die_ema_c", (double)snap->die_ema_c);
    else                         bb_http_resp_json_obj_set_null(obj, "die_ema_c");
    if (snap->vr_ema_c >= 0.0f)  bb_http_resp_json_obj_set_num(obj, "vr_ema_c",  (double)snap->vr_ema_c);
    else                         bb_http_resp_json_obj_set_null(obj, "vr_ema_c");
    if (snap->pid_input_c >= 0.0f) bb_http_resp_json_obj_set_num(obj, "pid_input_c", (double)snap->pid_input_c);
    else                           bb_http_resp_json_obj_set_null(obj, "pid_input_c");
    bb_http_resp_json_obj_set_str(obj, "pid_input_src", snap->pid_input_src);
}
#endif /* ASIC_CHIP */

/* ============================================================================
 * /api/diag/benchmark  (TA-33)
 * ========================================================================= */

/* Map sha_overlap_state_t to JSON string ("safe", "unsafe", "unknown") */
static const char *s_overlap_str(sha_overlap_state_t s)
{
    switch (s) {
        case SHA_OVERLAP_SAFE:   return "safe";
        case SHA_OVERLAP_UNSAFE: return "unsafe";
        default:                 return "unknown";
    }
}

bb_err_t diag_bench_parse_request(const char *body, int body_len, uint32_t *out_iters)
{
    *out_iters = DIAG_BENCH_ITERS_DEFAULT;

    /* Empty body → use default */
    if (!body || body_len <= 0) return BB_OK;

    bb_json_t root = bb_json_parse(body, 0);
    if (!root) return BB_ERR_INVALID_ARG;

    bb_json_t j = bb_json_obj_get_item(root, "iters");
    if (j) {
        if (!bb_json_item_is_number(j)) {
            bb_json_free(root);
            return BB_ERR_INVALID_ARG;
        }
        double v = bb_json_item_get_double(j);
        uint32_t iters = (uint32_t)v;
        if (iters < DIAG_BENCH_ITERS_MIN || iters > DIAG_BENCH_ITERS_MAX) {
            bb_json_free(root);
            return BB_ERR_NO_SPACE;   /* sentinel for out-of-range */
        }
        *out_iters = iters;
    }

    bb_json_free(root);
    return BB_OK;
}

void emit_diag_bench_json(bb_http_json_obj_stream_t *obj, const diag_bench_snapshot_t *s)
{
    bb_http_resp_json_obj_set_int(obj, "iters",               (int64_t)s->iters);
    bb_http_resp_json_obj_set_int(obj, "duration_us",         s->duration_us);
    bb_http_resp_json_obj_set_num(obj, "us_per_op",           s->us_per_op);
    bb_http_resp_json_obj_set_num(obj, "khs",                 s->khs);
    bb_http_resp_json_obj_set_num(obj, "sha_ops_per_sec",    s->sha_ops_per_sec);
    bb_http_resp_json_obj_set_str(obj, "backend",             s->backend ? s->backend : "sw");

    /* Adaptive convergence fields */
    bb_http_resp_json_obj_set_bool(obj, "settled",            s->settled);
    bb_http_resp_json_obj_set_int(obj, "settled_after_iters", (int64_t)s->settled_after_iters);
    bb_http_resp_json_obj_set_int(obj, "settled_iters",       (int64_t)s->settled_iters);
    bb_http_resp_json_obj_set_int(obj, "settled_total_us",    s->settled_total_us);

    bb_http_resp_json_obj_set_obj_begin(obj, "canary");
    bb_http_resp_json_obj_set_str(obj, "text_overlap", s_overlap_str(s->text_overlap_state));
    bb_http_resp_json_obj_set_str(obj, "h_write",      s_overlap_str(s->h_write_state));
    bb_http_resp_json_obj_set_obj_end(obj);

    if (s->has_asic_active) {
        bb_http_resp_json_obj_set_bool(obj, "asic_active", s->asic_active);
    }
}
