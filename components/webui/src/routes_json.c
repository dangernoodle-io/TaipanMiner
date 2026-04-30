/*
 * routes_json.c — pure JSON-builder functions for TaipanMiner-owned GET routes.
 *
 * No ESP-IDF includes, no I/O, no mutexes — host-compilable.
 * Builders write into a caller-provided bb_json_t root.
 */

#include "routes_json.h"
#include "bb_json.h"
#include "work.h"       /* bytes_to_hex */
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * /api/stats
 * ========================================================================= */

void build_stats_json(const stats_snapshot_t *s, bb_json_t root)
{
    int64_t uptime_s = (s->session_start_us > 0)
                       ? (s->now_us - s->session_start_us) / 1000000
                       : 0;
    int64_t last_share_ago_s = (s->last_share_us > 0)
                               ? (s->now_us - s->last_share_us) / 1000000
                               : -1;

    bb_json_obj_set_number(root, "hashrate",        s->hw_rate);
    bb_json_obj_set_number(root, "hashrate_avg",    s->hw_ema);
    bb_json_obj_set_number(root, "temp_c",          (double)s->temp_c);
    bb_json_obj_set_number(root, "shares",          s->hw_shares);
    bb_json_obj_set_number(root, "session_shares",  s->session_shares);
    bb_json_obj_set_number(root, "session_rejected", s->session_rejected);

    bb_json_t rejected = bb_json_obj_new();
    bb_json_obj_set_number(rejected, "total",           (double)s->session_rejected);
    bb_json_obj_set_number(rejected, "job_not_found",   (double)s->session_rejected_job_not_found);
    bb_json_obj_set_number(rejected, "low_difficulty",  (double)s->session_rejected_low_difficulty);
    bb_json_obj_set_number(rejected, "duplicate",       (double)s->session_rejected_duplicate);
    bb_json_obj_set_number(rejected, "stale_prevhash",  (double)s->session_rejected_stale_prevhash);
    bb_json_obj_set_number(rejected, "other",           (double)s->session_rejected_other);
    bb_json_obj_set_number(rejected, "other_last_code", (double)s->session_rejected_other_last_code);
    bb_json_obj_set_obj(root, "rejected", rejected);

    bb_json_obj_set_number(root, "last_share_ago_s",  (double)last_share_ago_s);
    bb_json_obj_set_number(root, "lifetime_shares",   s->lifetime_shares);
    bb_json_obj_set_number(root, "best_diff",         s->best_diff);
    bb_json_obj_set_number(root, "uptime_s",          (double)uptime_s);

#ifdef ASIC_CHIP
    if (s->asic_freq_cfg > 0) {
        double expected_ghs = (double)s->asic_freq_cfg
                              * (double)s->asic_small_cores
                              * (double)s->asic_count / 1000.0;
        bb_json_obj_set_number(root, "expected_ghs", expected_ghs);
    } else {
        bb_json_obj_set_null(root, "expected_ghs");
    }

    bb_json_obj_set_number(root, "asic_hashrate",     s->asic_rate);
    bb_json_obj_set_number(root, "asic_hashrate_avg", s->asic_ema);
    bb_json_obj_set_number(root, "asic_shares",       s->asic_shares);
    bb_json_obj_set_number(root, "asic_temp_c",       (double)s->asic_temp_c);
    if (s->asic_freq_cfg >= 0) {
        bb_json_obj_set_number(root, "asic_freq_configured_mhz", (double)s->asic_freq_cfg);
    } else {
        bb_json_obj_set_null(root, "asic_freq_configured_mhz");
    }
    if (s->asic_freq_eff >= 0) {
        bb_json_obj_set_number(root, "asic_freq_effective_mhz", (double)s->asic_freq_eff);
    } else {
        bb_json_obj_set_null(root, "asic_freq_effective_mhz");
    }
    bb_json_obj_set_number(root, "asic_small_cores", s->asic_small_cores);
    bb_json_obj_set_number(root, "asic_count",       s->asic_count);
    if (s->asic_total_valid) {
        bb_json_obj_set_number(root, "asic_total_ghs",      (double)s->asic_total_ghs);
        bb_json_obj_set_number(root, "asic_hw_error_pct",   (double)s->asic_hw_error_pct);
        bb_json_obj_set_number(root, "asic_total_ghs_1m",   (double)s->asic_total_ghs_1m);
        bb_json_obj_set_number(root, "asic_total_ghs_10m",  (double)s->asic_total_ghs_10m);
        bb_json_obj_set_number(root, "asic_total_ghs_1h",   (double)s->asic_total_ghs_1h);
        bb_json_obj_set_number(root, "asic_hw_error_pct_1m",  (double)s->asic_hw_error_pct_1m);
        bb_json_obj_set_number(root, "asic_hw_error_pct_10m", (double)s->asic_hw_error_pct_10m);
        bb_json_obj_set_number(root, "asic_hw_error_pct_1h",  (double)s->asic_hw_error_pct_1h);
    } else {
        bb_json_obj_set_null(root, "asic_total_ghs");
        bb_json_obj_set_null(root, "asic_hw_error_pct");
        bb_json_obj_set_null(root, "asic_total_ghs_1m");
        bb_json_obj_set_null(root, "asic_total_ghs_10m");
        bb_json_obj_set_null(root, "asic_total_ghs_1h");
        bb_json_obj_set_null(root, "asic_hw_error_pct_1m");
        bb_json_obj_set_null(root, "asic_hw_error_pct_10m");
        bb_json_obj_set_null(root, "asic_hw_error_pct_1h");
    }

    bb_json_t chips_arr = bb_json_arr_new();
    for (int c = 0; c < s->n_chips; c++) {
        bb_json_t chip_obj = bb_json_obj_new();
        bb_json_obj_set_number(chip_obj, "idx",         c);
        bb_json_obj_set_number(chip_obj, "total_ghs",   (double)s->chips[c].total_ghs);
        bb_json_obj_set_number(chip_obj, "error_ghs",   (double)s->chips[c].error_ghs);
        bb_json_obj_set_number(chip_obj, "hw_err_pct",  (double)s->chips[c].hw_err_pct);
        bb_json_obj_set_number(chip_obj, "total_raw",   (double)s->chips[c].total_raw);
        bb_json_obj_set_number(chip_obj, "error_raw",   (double)s->chips[c].error_raw);
        bb_json_obj_set_number(chip_obj, "total_drops", s->chips[c].total_drops);
        bb_json_obj_set_number(chip_obj, "error_drops", s->chips[c].error_drops);

        if (s->chips[c].last_drop_us == 0 || s->now_us < (int64_t)s->chips[c].last_drop_us) {
            bb_json_obj_set_null(chip_obj, "last_drop_ago_s");
        } else {
            uint64_t ago_us = (uint64_t)s->now_us - s->chips[c].last_drop_us;
            bb_json_obj_set_number(chip_obj, "last_drop_ago_s", (double)(ago_us / 1000000ULL));
        }

        bb_json_t domains_arr = bb_json_arr_new();
        for (int d = 0; d < 4; d++) {
            bb_json_arr_append_number(domains_arr, (double)s->chips[c].domain_ghs[d]);
        }
        bb_json_obj_set_arr(chip_obj, "domain_ghs", domains_arr);

        bb_json_t domains_drops_arr = bb_json_arr_new();
        for (int d = 0; d < 4; d++) {
            bb_json_arr_append_number(domains_drops_arr, s->chips[c].domain_drops[d]);
        }
        bb_json_obj_set_arr(chip_obj, "domain_drops", domains_drops_arr);

        bb_json_arr_append_obj(chips_arr, chip_obj);
    }
    bb_json_obj_set_arr(root, "asic_chips", chips_arr);
#else
    bb_json_obj_set_number(root, "expected_ghs", 0.000223);
#endif
}

/* ============================================================================
 * /api/pool
 * ========================================================================= */

void build_pool_json(const pool_snapshot_t *s, bb_json_t root)
{
    bb_json_obj_set_string(root, "host",   s->host);
    bb_json_obj_set_number(root, "port",   (double)s->port);
    bb_json_obj_set_string(root, "worker", s->worker);
    bb_json_obj_set_string(root, "wallet", s->wallet);
    bb_json_obj_set_bool(root,   "connected", s->connected);

    if (s->has_session_start) {
        bb_json_obj_set_number(root, "session_start_ago_s",
                               (double)s->session_start_ago_s);
    } else {
        bb_json_obj_set_null(root, "session_start_ago_s");
    }

    bb_json_obj_set_number(root, "current_difficulty", s->current_difficulty);

    if (s->latency_ms >= 0) {
        bb_json_obj_set_number(root, "latency_ms", (double)s->latency_ms);
    } else {
        bb_json_obj_set_null(root, "latency_ms");
    }

    if (s->extranonce1_len > 0) {
        char en1_hex[2 * ROUTES_JSON_EXTRANONCE1_MAX + 1];
        bytes_to_hex(s->extranonce1, s->extranonce1_len, en1_hex);
        bb_json_obj_set_string(root, "extranonce1", en1_hex);
        bb_json_obj_set_number(root, "extranonce2_size", (double)s->extranonce2_size);
        if (s->version_mask != 0) {
            char vm_hex[9];
            snprintf(vm_hex, sizeof(vm_hex), "%08lx", (unsigned long)s->version_mask);
            bb_json_obj_set_string(root, "version_mask", vm_hex);
        } else {
            bb_json_obj_set_null(root, "version_mask");
        }
    } else {
        bb_json_obj_set_null(root, "extranonce1");
        bb_json_obj_set_null(root, "extranonce2_size");
        bb_json_obj_set_null(root, "version_mask");
    }

    if (s->has_notify) {
        bb_json_t nobj = bb_json_obj_new();
        bb_json_obj_set_string(nobj, "job_id", s->job_id);

        char prevhash_hex[65];
        bytes_to_hex(s->prevhash, 32, prevhash_hex);
        bb_json_obj_set_string(nobj, "prev_hash", prevhash_hex);

        char coinb1_hex[2 * ROUTES_JSON_MAX_COINB + 1];
        bytes_to_hex(s->coinb1, s->coinb1_len, coinb1_hex);
        bb_json_obj_set_string(nobj, "coinb1", coinb1_hex);

        char coinb2_hex[2 * ROUTES_JSON_MAX_COINB + 1];
        bytes_to_hex(s->coinb2, s->coinb2_len, coinb2_hex);
        bb_json_obj_set_string(nobj, "coinb2", coinb2_hex);

        bb_json_t mb = bb_json_arr_new();
        for (size_t i = 0; i < s->merkle_count; i++) {
            char br_hex[65];
            bytes_to_hex(s->merkle_branches[i], 32, br_hex);
            bb_json_arr_append_string(mb, br_hex);
        }
        bb_json_obj_set_arr(nobj, "merkle_branches", mb);

        char hex8[9];
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)s->version);
        bb_json_obj_set_string(nobj, "version", hex8);
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)s->nbits);
        bb_json_obj_set_string(nobj, "nbits", hex8);
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)s->ntime);
        bb_json_obj_set_string(nobj, "ntime", hex8);

        bb_json_obj_set_bool(nobj, "clean_jobs", s->clean_jobs);
        bb_json_obj_set_obj(root, "notify", nobj);
    } else {
        bb_json_obj_set_null(root, "notify");
    }

    /* active_pool_idx — -1 if not connected */
    if (s->active_pool_idx >= 0) {
        bb_json_obj_set_number(root, "active_pool_idx", (double)s->active_pool_idx);
    } else {
        bb_json_obj_set_null(root, "active_pool_idx");
    }

    /* TA-306: extranonce.subscribe status for the active session */
    {
        const char *sub_str;
        switch (s->extranonce_subscribe_status) {
            case 1:  sub_str = "pending";  break;
            case 2:  sub_str = "active";   break;
            case 3:  sub_str = "rejected"; break;
            default: sub_str = "off";      break;
        }
        bb_json_obj_set_string(root, "extranonce_subscribe_status", sub_str);
    }

    /* configured pools — expose persisted config (TA-290/TA-202) */
    bb_json_t cfg_obj = bb_json_obj_new();
    for (int i = 0; i < 2; i++) {
        if (s->configured[i].configured) {
            bb_json_t pool_cfg = bb_json_obj_new();
            bb_json_obj_set_string(pool_cfg, "host",   s->configured[i].host);
            bb_json_obj_set_number(pool_cfg, "port",   (double)s->configured[i].port);
            bb_json_obj_set_string(pool_cfg, "worker", s->configured[i].worker);
            bb_json_obj_set_string(pool_cfg, "wallet", s->configured[i].wallet);
            bb_json_obj_set_bool(pool_cfg, "extranonce_subscribe",
                                 s->configured[i].extranonce_subscribe);
            bb_json_obj_set_bool(pool_cfg, "decode_coinbase",
                                 s->configured[i].decode_coinbase);
            bb_json_obj_set_obj(cfg_obj, (i == 0) ? "primary" : "fallback", pool_cfg);
        } else {
            bb_json_obj_set_null(cfg_obj, (i == 0) ? "primary" : "fallback");
        }
    }
    bb_json_obj_set_obj(root, "configured", cfg_obj);
}

/* ============================================================================
 * /api/diag/asic
 * ========================================================================= */

void build_diag_asic_json(const diag_asic_snapshot_t *s, bb_json_t root)
{
    bb_json_t arr = bb_json_arr_new();

    for (size_t i = 0; i < s->n_drops; i++) {
        const routes_json_drop_event_t *d = &s->drops[i];
        bb_json_t e = bb_json_obj_new();

        uint64_t age_us = (d->ts_us <= s->now_us) ? (s->now_us - d->ts_us) : 0;
        bb_json_obj_set_number(e, "ts_ago_s",  (double)(age_us / 1000000ULL));
        bb_json_obj_set_number(e, "chip",      (double)d->chip_idx);

        const char *kind_str;
        switch ((routes_json_drop_kind_t)d->kind) {
            case ROUTES_JSON_DROP_KIND_ERROR:  kind_str = "error";  break;
            case ROUTES_JSON_DROP_KIND_DOMAIN: kind_str = "domain"; break;
            default:                           kind_str = "total";  break;
        }
        bb_json_obj_set_string(e, "kind",      kind_str);
        bb_json_obj_set_number(e, "domain",    (double)d->domain_idx);
        bb_json_obj_set_number(e, "addr",      (double)d->asic_addr);
        bb_json_obj_set_number(e, "ghs",       (double)d->ghs);
        bb_json_obj_set_number(e, "delta",     (double)d->delta);
        bb_json_obj_set_number(e, "elapsed_s", (double)d->elapsed_s);
        bb_json_arr_append_obj(arr, e);
    }

    bb_json_obj_set_arr(root, "recent_drops", arr);
}

/* ============================================================================
 * /api/knot
 * ========================================================================= */

void build_knot_json(const knot_peer_t *peers, size_t n_peers, int64_t now_us, bb_json_t root)
{
    for (size_t i = 0; i < n_peers; i++) {
        const knot_peer_t *p = &peers[i];
        bb_json_t peer_obj = bb_json_obj_new();
        bb_json_obj_set_string(peer_obj, "instance",  p->instance_name);
        bb_json_obj_set_string(peer_obj, "hostname",  p->hostname);
        bb_json_obj_set_string(peer_obj, "ip",        p->ip4);
        bb_json_obj_set_string(peer_obj, "worker",    p->worker);
        bb_json_obj_set_string(peer_obj, "board",     p->board);
        bb_json_obj_set_string(peer_obj, "version",   p->version);
        bb_json_obj_set_string(peer_obj, "state",     p->state);

        int64_t seen_ago_s = (now_us - p->last_seen_us) / 1000000;
        bb_json_obj_set_number(peer_obj, "seen_ago_s", (double)seen_ago_s);

        bb_json_arr_append_obj(root, peer_obj);
    }
}

/* ============================================================================
 * /api/settings GET
 * ========================================================================= */

void build_settings_json(const settings_snapshot_t *s, bb_json_t root)
{
    bb_json_obj_set_string(root, "hostname",      s->hostname);
    bb_json_obj_set_bool(root,   "display_en",    s->display_en);
    bb_json_obj_set_bool(root,   "ota_skip_check", s->ota_skip_check);
}

/* ============================================================================
 * /api/power  (ASIC_CHIP only)
 * ========================================================================= */

#ifdef ASIC_CHIP
void build_power_json(const power_snapshot_t *s, bb_json_t root)
{
    if (s->vcore_mv >= 0) {
        bb_json_obj_set_number(root, "vcore_mv", s->vcore_mv);
    } else {
        bb_json_obj_set_null(root, "vcore_mv");
    }
    if (s->icore_ma >= 0) {
        bb_json_obj_set_number(root, "icore_ma", s->icore_ma);
    } else {
        bb_json_obj_set_null(root, "icore_ma");
    }
    if (s->pcore_mw >= 0) {
        bb_json_obj_set_number(root, "pcore_mw", s->pcore_mw);
    } else {
        bb_json_obj_set_null(root, "pcore_mw");
    }
    if (s->pcore_mw > 0 && s->asic_hashrate > 0) {
        bb_json_obj_set_number(root, "efficiency_jth",
                               (s->pcore_mw / 1000.0) / (s->asic_hashrate / 1e12));
    } else {
        bb_json_obj_set_null(root, "efficiency_jth");
    }
    if (s->vin_mv >= 0) {
        bb_json_obj_set_number(root, "vin_mv", s->vin_mv);
    } else {
        bb_json_obj_set_null(root, "vin_mv");
    }
    if (s->vin_mv >= 0) {
        bool vin_low = (s->vin_mv < (s->nominal_vin_mv + 500) * 87 / 100);
        bb_json_obj_set_bool(root, "vin_low", vin_low);
    } else {
        bb_json_obj_set_null(root, "vin_low");
    }
    if (s->board_temp_c >= 0.0f) {
        bb_json_obj_set_number(root, "board_temp_c", (double)s->board_temp_c);
    } else {
        bb_json_obj_set_null(root, "board_temp_c");
    }
    if (s->vr_temp_c >= 0.0f) {
        bb_json_obj_set_number(root, "vr_temp_c", (double)s->vr_temp_c);
    } else {
        bb_json_obj_set_null(root, "vr_temp_c");
    }
}

/* ============================================================================
 * /api/fan  (ASIC_CHIP only)
 * ========================================================================= */

void build_fan_json(const fan_snapshot_t *s, bb_json_t root)
{
    if (s->fan_rpm >= 0) {
        bb_json_obj_set_number(root, "rpm", s->fan_rpm);
    } else {
        bb_json_obj_set_null(root, "rpm");
    }
    if (s->fan_duty_pct >= 0) {
        bb_json_obj_set_number(root, "duty_pct", s->fan_duty_pct);
    } else {
        bb_json_obj_set_null(root, "duty_pct");
    }
}
#endif /* ASIC_CHIP */
