#ifdef ASIC_BM1368

#include "asic.h"
#include "bm137x_regs.h"
#include "bm1368.h"
#include "bb_power.h"
#include "bb_power_tps546.h"
#include "board.h"
#include "bb_system.h"

static bb_err_t chip_init(void)
{
    return bm137x_chip_init(&bm1368_regs,
                            (float)BM1368_DEFAULT_FREQ_MHZ,
                            BM1368_CHIP_COUNT);
}

static bb_err_t chip_quiesce(void)
{
    return bm137x_chip_quiesce(&bm1368_regs);
}

static bb_err_t chip_resume(void)
{
    return chip_init();
}

// AxeOS default-family protection profile (5V single-phase — applies to all bitaxe boards).
// target_mv differs per board but is set in cfg.target_mv; all other thresholds are the same.
static const bb_power_tps546_protect_t s_protect = {
    // VIN
    .vin_on_v             = 4.8f,
    .vin_off_v            = 4.5f,
    .vin_uv_warn_v        = 0.0f,   // skip
    .vin_ov_fault_v       = 6.5f,
    .vin_ov_fault_response = 0xB7,
    // VOUT absolute clamps
    .vout_scale_loop      = 0.25f,
    .vout_max_v           = 2.0f,
    .vout_min_v           = 1.0f,
    // VOUT OV/UV proportional factors (× target_V at encode time)
    .vout_ov_fault_factor = 1.25f,
    .vout_ov_warn_factor  = 1.16f,
    .vout_margin_high     = 1.10f,
    .vout_margin_low      = 0.90f,
    .vout_uv_warn_factor      = 0.90f,
    .vout_uv_fault_factor     = 0.75f,
    // hiccup on UV fault (auto-restart) instead of latch-off — transient dip self-recovers w/o power-cycle
    .vout_uv_fault_response   = 0xBF,
    // IOUT
    .iout_oc_warn_a       = 25.0f,  // oc_limit_a=30 (fault) set in outer cfg
    // Over-temperature
    .ot_warn_c            = 105,
    .ot_fault_c           = 145,
    .ot_fault_response    = 0xFF,
    // Soft-start
    .ton_delay_ms         = 0,      // skip
    .ton_rise_ms          = 3,
    .ton_max_fault_ms     = 0,      // skip
    .ton_max_fault_response = 0x3B,
    // SYNC disabled (single-phase, no external clock)
    .sync_config          = 0x10,
};

static bb_err_t tps546_vreg_init(i2c_master_bus_handle_t bus, uint16_t target_mv)
{
    bb_power_handle_t h;
    bb_power_tps546_cfg_t cfg = {
        .bus             = bus,
        .addr            = TPS546_I2C_ADDR,
        .target_mv       = target_mv,
        .switch_freq_khz = 650,
        .oc_limit_a      = 30,
        .oc_response     = 0xC0,
        .protect         = s_protect,
    };
    bb_err_t err = bb_power_tps546_open(&cfg, &h);
    if (err == BB_OK) {
        bb_power_set_primary(h);
    }
    return err;
}

static const asic_chip_ops_t s_bm1368_ops = {
    .chip_init        = chip_init,
    .chip_quiesce     = chip_quiesce,
    .chip_resume      = chip_resume,
    .vreg_init        = tps546_vreg_init,
    .fb_min           = BM1368_FB_MIN,
    .fb_max           = BM1368_FB_MAX,
    .default_mv       = BM1368_DEFAULT_MV,
    .default_freq_mhz = BM1368_DEFAULT_FREQ_MHZ,
    .chip_count       = BM1368_CHIP_COUNT,
    .chip_id          = BM1368_CHIP_ID,
    .job_interval_ms  = BM1368_JOB_INTERVAL_MS,
};

const asic_chip_ops_t *g_chip_ops = &s_bm1368_ops;

#endif // ASIC_BM1368
