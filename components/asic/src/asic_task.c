#include "asic_chip.h"
#ifdef ASIC_CHIP

#include "asic.h"
#include "asic_proto.h"
#include "asic_internal.h"
#include "asic_pause_coalesce.h"
#include "mining_avg.h"
#include "asic_drop_detect.h"
#include "asic_drop_log.h"
#include "asic_chip_routing.h"
#include "asic_nonce_dedup.h"
#include "share_validate.h"
#include "mining_pool_stats.h"
#include "crc.h"
#include "bb_power.h"
#include "bb_power_health.h"
#include "bb_fan.h"
#include "bb_fan_emc2101.h"
#include "config.h"
#include "mining.h"
#include "diag.h"
#include "stratum.h"
#include "work.h"
#include "sha256.h"
#include "board.h"
#include "board_gpio.h"
#include "bm1370.h"

#include "esp_log.h"
#include "esp_system.h"
#include "bb_log.h"
#include "bb_nv.h"
#include "bb_event.h"
#include "bb_byte_order.h"
#include "bb_wdt.h"
#include "esp_check.h"
#include "bb_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>

static const char *TAG = "asic";

// TA-318: vcore-collapse watchdog (bb_power_health, gated by CONFIG_TM_VCORE_WATCHDOG)
#ifdef CONFIG_TM_VCORE_WATCHDOG
#define VCORE_WD_NVS_NS  "taipanminer"
#define VCORE_WD_NVS_KEY "vcore_restart_count"

static bb_vcore_wd_state_t s_vcore_wd;       // zero-init valid per bb_power_health.h
static uint32_t s_vcore_restart_count;        // NVS-persisted burst counter (runtime mirror)
static bool s_vcore_backoff_logged;           // rate-limit the "budget exhausted" log line

// Expose restart count for telemetry extender in routes.c.
uint32_t asic_task_get_vcore_restart_count(void) { return s_vcore_restart_count; }
#else
uint32_t asic_task_get_vcore_restart_count(void) { return 0; }
#endif /* CONFIG_TM_VCORE_WATCHDOG */

// Clock source for bb_fan autofan PID — millisecond timestamp via bb_timer.
// Injected via bb_fan_autofan_set_clock() rather than bb_fan_autofan_inject_clock()
// so we don't depend on the ESP-IDF platform shim being compiled with CONFIG_BB_FAN_AUTOFAN.
#ifdef CONFIG_BB_FAN_AUTOFAN
static unsigned long s_fan_now_ms(void)
{
    return (unsigned long)(bb_timer_now_us() / 1000ULL);
}
#endif

// --- I2C bus handle (initialized in asic_init, shared with display) ---
static i2c_master_bus_handle_t s_i2c_bus;

// --- Active job table ---
// Size decoupled from wire-protocol modulus (ASIC_JOB_ID_MOD=128) to reclaim
// ~24 KB BSS. Slot is selected by real_job_id % ASIC_JOB_TABLE_SIZE; a
// generation counter + stored identity guard detect stale/recycled-slot nonces.
static mining_work_t s_job_table[ASIC_JOB_TABLE_SIZE];
static uint8_t       s_job_gen[ASIC_JOB_TABLE_SIZE];     // incremented on each overwrite
static uint8_t       s_job_id_seen[ASIC_JOB_TABLE_SIZE]; // raw real_job_id last stored here
static uint8_t s_next_job_id;

// asic_job_slot() is provided as asic_asic_job_slot() in asic_proto.h (public inline).
static uint32_t s_current_work_seq;
static double s_current_asic_diff = 0;

// --- Nonce dedup (ASIC loops nonce space quickly, reports same results) ---
static asic_nonce_dedup_t s_dedup;

// Debug counters for SHA256d filter
static uint32_t s_nonce_verify_pass;
static uint32_t s_nonce_verify_fail;

// TA-221: sanity clamps for register-derived GHS. BM1370 theoretical max at 650 MHz
// is ~1300 GH/s; BM1368 similar. Any computed hashrate above these thresholds is
// a register-read glitch (boot-phase corruption or data path issue), not real silicon.
#define ASIC_CHIP_GHS_SANITY_MAX   2000.0f  // per-chip cap
#define ASIC_DOMAIN_GHS_SANITY_MAX  500.0f  // per-domain cap (1/4 of chip)

// TA-223: warning rate-limit cooldown (per chip, per metric)
#define WARN_COOLDOWN_US (30ULL * 1000 * 1000)

static struct {
    uint64_t total_last_warn_us;
    uint64_t error_last_warn_us;
    uint64_t domain_last_warn_us[4];
} s_chip_warn[BOARD_ASIC_COUNT];

// TA-192: per-chip register-derived telemetry (polled every 5s)
static struct {
    uint32_t total_val;        // last REG_TOTAL_COUNT read
    uint32_t error_val;        // last REG_ERROR_COUNT read
    uint32_t total_val_base;   // counter value at first successful poll (post-ramp)
    uint32_t error_val_base;
    uint64_t total_time_us;    // esp_timer_get_time() at last total_val
    uint64_t error_time_us;
    float    total_ghs;
    float    error_ghs;
    uint8_t  total_samples;    // incremented on each read; GHS computed only when >= 2 (TA-221)
    uint8_t  error_samples;
    // Per-domain (BM1370 has 4 hash domains per chip)
    uint32_t domain_val[4];
    uint64_t domain_time_us[4];
    float    domain_ghs[4];
    uint8_t  domain_samples[4];
    // TA-223: drop counters for sanity-fail telemetry
    uint32_t total_drops;
    uint32_t error_drops;
    uint32_t domain_drops[4];
    // TA-237: timestamp of most-recent drop, any kind. 0 if never.
    uint64_t last_drop_us;
} s_chip_meas[BOARD_ASIC_COUNT];

// TA-238: ring buffer of recent telemetry-drop events for forensic readback
// without needing a live log subscriber.
static asic_drop_log_t s_drop_log;

// TA-196: rolling-window averages mirroring ESP-Miner's hashrate_monitor_task.c
#define ASIC_POLL_PERIOD_MS 5000
#define ASIC_AVG_1M_SIZE    MINING_AVG_1M_SIZE
#define ASIC_AVG_10M_SIZE   MINING_AVG_10M_SIZE
#define ASIC_AVG_1H_SIZE    MINING_AVG_1H_SIZE
#define ASIC_AVG_DIV_10M    MINING_AVG_DIV_10M
#define ASIC_AVG_DIV_1H     MINING_AVG_DIV_1H

static unsigned long s_avg_poll_count;
static float s_total_1m[ASIC_AVG_1M_SIZE];
static float s_total_10m[ASIC_AVG_10M_SIZE];
static float s_total_1h[ASIC_AVG_1H_SIZE];
static float s_total_10m_prev, s_total_1h_prev;
static float s_hw_err_1m[ASIC_AVG_1M_SIZE];
static float s_hw_err_10m[ASIC_AVG_10M_SIZE];
static float s_hw_err_1h[ASIC_AVG_1H_SIZE];
static float s_hw_err_10m_prev, s_hw_err_1h_prev;

/* Pool-effective rolling sampler (TA-363) */
static unsigned long s_pool_eff_poll_count = 0;
static float s_pool_eff_1m[ASIC_AVG_1M_SIZE];
static float s_pool_eff_10m[ASIC_AVG_10M_SIZE];
static float s_pool_eff_1h[ASIC_AVG_1H_SIZE];
static float s_pool_eff_10m_prev = NAN;
static float s_pool_eff_1h_prev = NAN;
static double s_pool_eff_prev_sum = 0.0;

/* Power rolling averages (TA-213) */
static unsigned long s_pcore_poll_count = 0;
static float s_pcore_1m[ASIC_AVG_1M_SIZE];
static float s_pcore_10m[ASIC_AVG_10M_SIZE];
static float s_pcore_1h[ASIC_AVG_1H_SIZE];
static float s_pcore_10m_prev = NAN, s_pcore_1h_prev = NAN;

// Register addresses for 5s polling loop
static const uint8_t s_poll_regs[] = {
    ASIC_REG_TOTAL_COUNT,
    ASIC_REG_ERROR_COUNT,
    ASIC_REG_DOMAIN_0_COUNT,
    ASIC_REG_DOMAIN_1_COUNT,
    ASIC_REG_DOMAIN_2_COUNT,
    ASIC_REG_DOMAIN_3_COUNT,
};

// Pause/resume coalesce state
static bool s_chip_quiesced = false;

// --- UART helpers ---
int asic_uart_read(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    return uart_read_bytes(ASIC_UART_NUM, buf, len, pdMS_TO_TICKS(timeout_ms));
}

void asic_uart_write(const uint8_t *buf, size_t len)
{
    uart_write_bytes(ASIC_UART_NUM, buf, len);
}

// --- Command send helper ---
void send_cmd(uint8_t cmd, uint8_t group, const uint8_t *data, uint8_t data_len)
{
    uint8_t pkt[64];
    size_t n = asic_build_cmd(pkt, sizeof(pkt), cmd, group, data, data_len);
    if (n > 0) {
        asic_uart_write(pkt, n);
    }
}

// --- Register write helper (broadcast) ---
void write_reg(uint8_t reg, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
    uint8_t data[6] = {0x00, reg, d0, d1, d2, d3};
    send_cmd(ASIC_CMD_WRITE, ASIC_GROUP_ALL, data, 6);
}

// --- Register write to specific chip ---
void write_reg_chip(uint8_t chip_addr, uint8_t reg, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
    uint8_t data[6] = {chip_addr, reg, d0, d1, d2, d3};
    send_cmd(ASIC_CMD_WRITE, ASIC_GROUP_SINGLE, data, 6);
}

// --- Register read (broadcast) — each chip responds with one cmd frame ---
static void read_reg(uint8_t reg)
{
    uint8_t data[2] = {0x00, reg};
    send_cmd(ASIC_CMD_READ, ASIC_GROUP_ALL, data, 2);
}

// --- Update ASIC ticket mask for dynamic pool difficulty ---
void set_ticket_mask(double difficulty)
{
    uint8_t mask[4];
    asic_difficulty_to_mask(difficulty, mask);
    write_reg(ASIC_REG_TICKET_MASK, mask[0], mask[1], mask[2], mask[3]);
    s_current_asic_diff = difficulty;
    bb_log_i(TAG, "ticket mask: diff=%.0f bytes=%02x%02x%02x%02x",
             difficulty, mask[0], mask[1], mask[2], mask[3]);
}

static void init_avg_buffers(void)
{
    s_avg_poll_count = 0;
    for (int i = 0; i < ASIC_AVG_1M_SIZE; i++)  { s_total_1m[i]  = NAN; s_hw_err_1m[i]  = NAN; }
    for (int i = 0; i < ASIC_AVG_10M_SIZE; i++) { s_total_10m[i] = NAN; s_hw_err_10m[i] = NAN; }
    for (int i = 0; i < ASIC_AVG_1H_SIZE; i++)  { s_total_1h[i]  = NAN; s_hw_err_1h[i]  = NAN; }
    s_total_10m_prev = NAN;
    s_total_1h_prev  = NAN;
    s_hw_err_10m_prev = NAN;
    s_hw_err_1h_prev  = NAN;
    s_pool_eff_poll_count = 0;
    for (int i = 0; i < ASIC_AVG_1M_SIZE; i++)  s_pool_eff_1m[i]  = NAN;
    for (int i = 0; i < ASIC_AVG_10M_SIZE; i++) s_pool_eff_10m[i] = NAN;
    for (int i = 0; i < ASIC_AVG_1H_SIZE; i++)  s_pool_eff_1h[i]  = NAN;
    s_pool_eff_10m_prev = NAN;
    s_pool_eff_1h_prev = NAN;
    s_pool_eff_prev_sum = 0.0;
    s_pcore_poll_count = 0;
    for (int i = 0; i < ASIC_AVG_1M_SIZE; i++)  s_pcore_1m[i]  = NAN;
    for (int i = 0; i < ASIC_AVG_10M_SIZE; i++) s_pcore_10m[i] = NAN;
    for (int i = 0; i < ASIC_AVG_1H_SIZE; i++)  s_pcore_1h[i]  = NAN;
    s_pcore_10m_prev = NAN;
    s_pcore_1h_prev = NAN;
}

// Abandon all in-flight work: invalidate every job slot and clear the nonce dedup. Shared by
// the full re-init reset and the stratum "clean job" (new block) path so stale work and dedup
// state are dropped identically wherever active work is discarded.
static void asic_invalidate_jobs(void)
{
    memset(s_job_table,   0, sizeof(s_job_table));
    memset(s_job_gen,     0, sizeof(s_job_gen));
    memset(s_job_id_seen, 0, sizeof(s_job_id_seen));
    asic_nonce_dedup_reset(&s_dedup);
}

// Single source of truth for a full work-dispatch re-init (cold-boot init + in-place
// recovery): abandon all work AND re-arm the dispatch gate (s_current_work_seq) and job-id
// counter so the next stratum work is dispatched fresh. After a warm recovery these are stale,
// so without this the re-inited chip gets NO new work (0 GH/s).
static void asic_reset_work_state(void)
{
    asic_invalidate_jobs();
    s_current_work_seq = 0;
    s_next_job_id = 0;
}

// Off-first rail cycle: drive ASIC_EN low to fully cut VCORE before re-enabling, then
// reset the ASIC. The TPS546 enable is the ASIC_EN pin (not PMBus OPERATION), so
// re-asserting EN high without first driving it low never clears a latched fault
// (OC_RESPONSE 0xC0 = latch-off) — previously only a mains power-cycle recovered the board.
// AxeOS cycles the enable on every boot. Shared by startup and in-place watchdog recovery.
static void asic_rail_cycle(void)
{
    gpio_set_direction(PIN_ASIC_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_ASIC_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(300));
    gpio_set_level(PIN_ASIC_EN, 1);
    bb_log_i(TAG, "ASIC EN: off->on rail cycle (power on)");
    vTaskDelay(pdMS_TO_TICKS(50));

    gpio_set_direction(PIN_ASIC_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_ASIC_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_ASIC_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    bb_log_i(TAG, "ASIC reset complete");
}

// --- asic_init ---
bb_err_t asic_init(void)
{
    bb_log_i(TAG, "initializing ASIC subsystem");

    // mining_run_self_tests() runs in app_main before any task starts;
    // assert the gate is clean rather than re-running (TA-347)
    if (mining_sha_self_test_failed()) {
        bb_log_e(TAG, "SHA self-test gate failed before asic_init; aborting");
        return BB_OK;
    }

    // 1. UART init
    uart_config_t uart_cfg = {
        .baud_rate = ASIC_BAUD_INIT,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
    };
    ESP_RETURN_ON_ERROR(uart_param_config(ASIC_UART_NUM, &uart_cfg), TAG, "uart config");
    ESP_RETURN_ON_ERROR(uart_set_pin(ASIC_UART_NUM, PIN_ASIC_TX, PIN_ASIC_RX,
                                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart pins");
    ESP_RETURN_ON_ERROR(uart_driver_install(ASIC_UART_NUM, 2048, 2048, 0, NULL, 0), TAG, "uart install");
    bb_log_i(TAG, "UART%d ready at %d baud", ASIC_UART_NUM, ASIC_BAUD_INIT);

    // 2. GPIO: power enable + reset (off-first rail cycle — see asic_rail_cycle)
    asic_rail_cycle();

    // 3. I2C bus
#ifdef HAS_I2C
    if (s_i2c_bus != NULL) {
        bb_log_i(TAG, "I2C bus already initialized");
    } else {
        if (!board_gpio_valid(PIN_I2C_SDA, "I2C_SDA") || !board_gpio_valid(PIN_I2C_SCL, "I2C_SCL")) {
            bb_log_w(TAG, "I2C GPIO validation failed, skipping I2C init");
        } else {
            i2c_master_bus_config_t bus_cfg = {
                .i2c_port = I2C_BUS_NUM,
                .sda_io_num = PIN_I2C_SDA,
                .scl_io_num = PIN_I2C_SCL,
                .clk_source = I2C_CLK_SRC_DEFAULT,
                .glitch_ignore_cnt = 7,
                .flags.enable_internal_pullup = true,
            };
            ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "i2c bus");
        }
    }
    if (s_i2c_bus != NULL) {
        bb_log_i(TAG, "I2C bus ready (SDA=%d SCL=%d)", PIN_I2C_SDA, PIN_I2C_SCL);
    }

    // 4. Voltage regulator
    if (s_i2c_bus != NULL) {
        ESP_RETURN_ON_ERROR(g_chip_ops->vreg_init(s_i2c_bus, g_chip_ops->default_mv), TAG, "vreg");
        vTaskDelay(pdMS_TO_TICKS(50));
    } else {
        bb_log_w(TAG, "I2C bus not available, skipping voltage regulator init");
    }

    // 5. EMC2101 temp sensor + fan (via bb_fan HAL)
    if (s_i2c_bus != NULL) {
        bb_fan_handle_t fan_h;
        bb_fan_emc2101_cfg_t fan_cfg = {
            .bus      = s_i2c_bus,
            .addr     = EMC2101_I2C_ADDR,
#ifdef EMC2101_IDEALITY_FACTOR
            .ideality = EMC2101_IDEALITY_FACTOR,
#else
            .ideality = 0,
#endif
#ifdef EMC2101_BETA_COMPENSATION
            .beta     = EMC2101_BETA_COMPENSATION,
#else
            .beta     = 0,
#endif
        };
        ESP_RETURN_ON_ERROR(bb_fan_emc2101_open(&fan_cfg, &fan_h), TAG, "emc2101");
        bb_fan_set_primary(fan_h);
        // Failsafe: start at 100% until autofan takes over
        ESP_RETURN_ON_ERROR(bb_fan_set_duty_pct(fan_h, 100), TAG, "fan on");
#ifdef CONFIG_BB_FAN_AUTOFAN
        // Inject bb_timer-backed ms clock into autofan PID (required for 5s gating).
        // Use bb_fan_autofan_set_clock directly so the shim file (bb_fan_clock_espidf.c)
        // doesn't need CONFIG_BB_FAN_AUTOFAN at compile time.
        bb_fan_autofan_set_clock(fan_h, s_fan_now_ms);
        // Load persisted autofan config from NVS and apply
        {
            bb_fan_autofan_cfg_t af_cfg = {
                .enabled      = config_autofan_enabled(),
                .die_target_c = (float)config_die_target_c(),
                .aux_target_c = (float)config_vr_target_c(),
                .min_pct      = (int)config_min_fan_pct(),
                .manual_pct   = (int)config_manual_fan_pct(),
            };
            bb_fan_set_autofan(fan_h, &af_cfg);
        }
#endif /* CONFIG_BB_FAN_AUTOFAN */
    } else {
        bb_log_w(TAG, "I2C bus not available, skipping EMC2101 init");
    }
#else
    bb_log_w(TAG, "HAS_I2C undefined, skipping I2C init");
#endif

    // 6. Chip init sequence
    ESP_RETURN_ON_ERROR(g_chip_ops->chip_init(), TAG, "chip init");

    // Initialize state
    asic_reset_work_state();
    init_avg_buffers();

#ifdef CONFIG_TM_VCORE_WATCHDOG
    // TA-318: Load persisted burst counter from NVS so the budget survives reboots.
    // Seed s_vcore_wd.burst_count so BB policy stays authoritative across restarts.
    s_vcore_restart_count = 0;
    bb_nv_get_u32(VCORE_WD_NVS_NS, VCORE_WD_NVS_KEY, &s_vcore_restart_count, 0);
    s_vcore_wd.burst_count = (int)s_vcore_restart_count;
    s_vcore_backoff_logged = false;
    bb_log_i(TAG, "vcore watchdog: loaded burst_count=%u from NVS", s_vcore_restart_count);
#endif

    bb_log_i(TAG, "ASIC subsystem ready");
    return BB_OK;
}

#ifdef CONFIG_TM_VCORE_WATCHDOG
// In-place vcore recovery: physical rail-cycle (clears a latched TPS546 fault that a
// PMBus-only reset can't, since the rail enable is the ASIC_EN pin) → TPS546 reprogram
// (it powers up at factory defaults after the EN cut) → ASIC PLL re-ramp. No reboot.
static bb_err_t asic_vcore_recover_inplace(void)
{
    // 1. Rail cycle via ASIC_EN (shared with startup)
    asic_rail_cycle();
    // 2. Reprogram the TPS546 (factory defaults after the EN cut)
    if (g_chip_ops->vreg_recover != NULL) {
        bb_err_t e = g_chip_ops->vreg_recover(s_i2c_bus, g_chip_ops->default_mv);
        if (e != BB_OK) return e;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    // 4. Re-ramp the ASIC PLL to operating frequency
    bb_err_t e = g_chip_ops->chip_resume();
    if (e != BB_OK) return e;

    // 5. Reset work-dispatch state (work_seq gate, job table, nonce dedup) so dispatch and
    //    nonce validation resume cleanly — without the work_seq reset the chip gets no work.
    asic_reset_work_state();
    return BB_OK;
}
#endif /* CONFIG_TM_VCORE_WATCHDOG */

// --- Mining task ---
void asic_mining_task(void *arg)
{
    bb_log_i(TAG, "ASIC mining task started");
    bb_wdt_task_subscribe();
    asic_drop_log_reset(&s_drop_log);

    mining_work_t work;
    TickType_t last_temp_tick = 0;
    TickType_t last_hashrate_tick = xTaskGetTickCount();
    TickType_t last_reg_poll = 0;
    uint32_t nonces_since_log = 0;


    for (;;) {
        // Pet the watchdog unconditionally so empty-queue spins (e.g. during
        // stratum disconnect) don't starve the WDT and trigger a task_wdt reboot.
        // xQueuePeek uses a 100ms timeout, so this fires at least every 100ms —
        // well within the 5s WDT window.
        bb_wdt_task_feed();

        // Pause/resume coalescing. bb_ota_pull triggers a check-phase pause/resume
        // immediately followed by an install-phase pause — during the ~8s freq
        // ramp of chip_resume, asic_task can't call mining_pause_check, so the
        // second pause's ACK window expires. Keep the chip in CMD_INACTIVE across
        // rapid-fire cycles and only run the re-init ramp when we're truly clear.
        asic_pause_action_t action = asic_pause_coalesce_next(mining_pause_pending(), &s_chip_quiesced);
        switch (action) {
        case ASIC_PAUSE_ACTION_QUIESCE_AND_ACK:
            g_chip_ops->chip_quiesce();
            // mining_pause_check() self-feeds the Task WDT via bb_wdt_park_wait
            // inside mining_pause.c — no delete/add bracket needed here.
            mining_pause_check();
            break;
        case ASIC_PAUSE_ACTION_ACK_ONLY:
            // mining_pause_check() self-feeds the Task WDT via bb_wdt_park_wait
            // inside mining_pause.c — no delete/add bracket needed here.
            mining_pause_check();
            break;
        case ASIC_PAUSE_ACTION_RESUME:
            g_chip_ops->chip_resume();
            break;
        case ASIC_PAUSE_ACTION_NONE:
        default:
            break;
        }

        // 1. Peek for work
        if (xQueuePeek(work_queue, &work, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        if (!is_target_valid(work.target)) {
            bb_log_w(TAG, "invalid target from queue, waiting for fresh work");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Set ASIC ticket mask once (fixed at ASIC_TICKET_DIFF)
        if (s_current_asic_diff == 0) {
            set_ticket_mask(ASIC_TICKET_DIFF);
        }

        // 2. Dispatch new job if work changed (new pool job or extranonce2 roll)
        if (work.work_seq != s_current_work_seq) {
            // Clean job (new block): abandon all active work; otherwise just clear dedup.
            if (work.clean) {
                asic_invalidate_jobs();
            } else {
                asic_nonce_dedup_reset(&s_dedup);
            }

            // Cycle job ID
            s_next_job_id = (s_next_job_id + ASIC_JOB_ID_STEP) % ASIC_JOB_ID_MOD;

            // Extract and send job
            asic_job_t job;
            asic_extract_job(work.header, s_next_job_id, &job);

            uint8_t pkt[ASIC_JOB_PKT_LEN];
            asic_build_job(pkt, sizeof(pkt), &job);
            asic_uart_write(pkt, ASIC_JOB_PKT_LEN);

            // Store in job table slot (generation guard: detect stale recycled slots)
            size_t slot = asic_job_slot(s_next_job_id);
            memcpy(&s_job_table[slot], &work, sizeof(work));
            s_job_id_seen[slot] = s_next_job_id;
            s_job_gen[slot]++;
            s_current_work_seq = work.work_seq;

            bb_log_d(TAG, "job dispatched (id=%u hw_id=%u)", 0, s_next_job_id);
        }

        // 3. Try to read nonce response
        uint8_t rx[ASIC_NONCE_LEN];
        int n = asic_uart_read(rx, ASIC_NONCE_LEN, 100);
        if (n == ASIC_NONCE_LEN) {
            asic_nonce_t nonce;
            if (!asic_parse_nonce(rx, n, &nonce)) {
                bb_log_w(TAG, "bad preamble: %02X %02X", rx[0], rx[1]);
                uart_flush(ASIC_UART_NUM);
                continue;
            }

            uint8_t crc_val = crc5(rx + 2, 9);
            bb_log_d(TAG, "rx: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X crc5=%u flags=%02X",
                     rx[0],rx[1],rx[2],rx[3],rx[4],rx[5],rx[6],rx[7],rx[8],rx[9],rx[10],
                     crc_val, nonce.crc_flags);

            // CRC5 check
            if (crc_val != 0) {
                bb_log_w(TAG, "nonce CRC5 failed (got %u)", crc_val);
                continue;
            }

            // Check if job response (bit 7 of crc_flags)
            if (!(nonce.crc_flags & 0x80)) {
                // Command response: reinterpret bytes per ESP-Miner's bm1370_asic_result_cmd_t.
                // rx[2..5] = value (big-endian; ASIC returns network byte order — ntohl equivalent),
                // rx[6] = asic_address, rx[7] = register_address
                uint32_t value = bb_load_be32(rx + 2);
                uint8_t asic_addr = rx[6];
                uint8_t reg_addr  = rx[7];

                int chip_idx = asic_chip_routing_index(asic_addr, BOARD_ASIC_COUNT);
                if (chip_idx < 0) {
                    continue;
                }

                uint64_t now_us = bb_timer_now_us();
                static const uint64_t HASH_CNT_LSB = 1ULL << 32;

                if (reg_addr == ASIC_REG_TOTAL_COUNT) {
                    if (s_chip_meas[chip_idx].total_samples >= 2) {
                        uint32_t delta = value - s_chip_meas[chip_idx].total_val;
                        float seconds = (float)(now_us - s_chip_meas[chip_idx].total_time_us) / 1e6f;
                        if (seconds > 0.001f) {
                            float ghs = (float)delta * (float)HASH_CNT_LSB / seconds / 1e9f;
                            asic_drop_detect_step_t step = asic_drop_detect_evaluate(
                                ghs, ASIC_CHIP_GHS_SANITY_MAX,
                                bb_timer_now_us(),
                                s_chip_warn[chip_idx].total_last_warn_us,
                                WARN_COOLDOWN_US);
                            if (step.accept) {
                                s_chip_meas[chip_idx].total_ghs = ghs;
                            } else {
                                s_chip_meas[chip_idx].total_drops++;
                                s_chip_meas[chip_idx].last_drop_us = now_us;
                                asic_drop_event_t ev = {
                                    .ts_us = now_us, .chip_idx = (uint8_t)chip_idx,
                                    .kind = ASIC_DROP_KIND_TOTAL, .domain_idx = 0,
                                    .asic_addr = asic_addr, .ghs = ghs,
                                    .delta = delta, .elapsed_s = (float)seconds,
                                };
                                asic_drop_log_push(&s_drop_log, &ev);
                                if (step.should_warn) {
                                    bb_log_w(TAG, "chip %d total sanity fail: ghs=%.1f delta=0x%08" PRIx32
                                                  " elapsed=%.3fs addr=0x%02X drops=%" PRIu32 " — dropped",
                                             chip_idx, ghs, delta, (float)seconds, asic_addr,
                                             s_chip_meas[chip_idx].total_drops);
                                    s_chip_warn[chip_idx].total_last_warn_us = step.new_last_warn_us;
                                }
                            }
                        }
                    } else if (s_chip_meas[chip_idx].total_samples == 0) {
                        s_chip_meas[chip_idx].total_val_base = value;  // warmup baseline
                    }
                    if (s_chip_meas[chip_idx].total_samples < 3) s_chip_meas[chip_idx].total_samples++;
                    s_chip_meas[chip_idx].total_val = value;
                    s_chip_meas[chip_idx].total_time_us = now_us;
                } else if (reg_addr == ASIC_REG_ERROR_COUNT) {
                    if (s_chip_meas[chip_idx].error_samples >= 2) {
                        uint32_t delta = value - s_chip_meas[chip_idx].error_val;
                        float seconds = (float)(now_us - s_chip_meas[chip_idx].error_time_us) / 1e6f;
                        if (seconds > 0.001f) {
                            float ghs = (float)delta * (float)HASH_CNT_LSB / seconds / 1e9f;
                            asic_drop_detect_step_t step = asic_drop_detect_evaluate(
                                ghs, ASIC_CHIP_GHS_SANITY_MAX,
                                bb_timer_now_us(),
                                s_chip_warn[chip_idx].error_last_warn_us,
                                WARN_COOLDOWN_US);
                            if (step.accept) {
                                s_chip_meas[chip_idx].error_ghs = ghs;
                            } else {
                                s_chip_meas[chip_idx].error_drops++;
                                s_chip_meas[chip_idx].last_drop_us = now_us;
                                asic_drop_event_t ev = {
                                    .ts_us = now_us, .chip_idx = (uint8_t)chip_idx,
                                    .kind = ASIC_DROP_KIND_ERROR, .domain_idx = 0,
                                    .asic_addr = asic_addr, .ghs = ghs,
                                    .delta = delta, .elapsed_s = (float)seconds,
                                };
                                asic_drop_log_push(&s_drop_log, &ev);
                                if (step.should_warn) {
                                    bb_log_w(TAG, "chip %d error sanity fail: ghs=%.1f delta=0x%08" PRIx32
                                                  " elapsed=%.3fs addr=0x%02X drops=%" PRIu32 " — dropped",
                                             chip_idx, ghs, delta, (float)seconds, asic_addr,
                                             s_chip_meas[chip_idx].error_drops);
                                    s_chip_warn[chip_idx].error_last_warn_us = step.new_last_warn_us;
                                }
                            }
                        }
                    } else if (s_chip_meas[chip_idx].error_samples == 0) {
                        s_chip_meas[chip_idx].error_val_base = value;  // warmup baseline
                    }
                    if (s_chip_meas[chip_idx].error_samples < 3) s_chip_meas[chip_idx].error_samples++;
                    s_chip_meas[chip_idx].error_val = value;
                    s_chip_meas[chip_idx].error_time_us = now_us;
                } else if (reg_addr >= ASIC_REG_DOMAIN_0_COUNT && reg_addr <= ASIC_REG_DOMAIN_3_COUNT) {
                    int d = reg_addr - ASIC_REG_DOMAIN_0_COUNT;
                    if (s_chip_meas[chip_idx].domain_samples[d] >= 2) {
                        uint32_t delta = value - s_chip_meas[chip_idx].domain_val[d];
                        float seconds = (float)(now_us - s_chip_meas[chip_idx].domain_time_us[d]) / 1e6f;
                        if (seconds > 0.001f) {
                            float ghs = (float)delta * (float)HASH_CNT_LSB / seconds / 1e9f;
                            asic_drop_detect_step_t step = asic_drop_detect_evaluate(
                                ghs, ASIC_DOMAIN_GHS_SANITY_MAX,
                                bb_timer_now_us(),
                                s_chip_warn[chip_idx].domain_last_warn_us[d],
                                WARN_COOLDOWN_US);
                            if (step.accept) {
                                s_chip_meas[chip_idx].domain_ghs[d] = ghs;
                            } else {
                                s_chip_meas[chip_idx].domain_drops[d]++;
                                s_chip_meas[chip_idx].last_drop_us = now_us;
                                asic_drop_event_t ev = {
                                    .ts_us = now_us, .chip_idx = (uint8_t)chip_idx,
                                    .kind = ASIC_DROP_KIND_DOMAIN, .domain_idx = (uint8_t)d,
                                    .asic_addr = asic_addr, .ghs = ghs,
                                    .delta = delta, .elapsed_s = (float)seconds,
                                };
                                asic_drop_log_push(&s_drop_log, &ev);
                                if (step.should_warn) {
                                    bb_log_w(TAG, "chip %d domain %d sanity fail: ghs=%.1f delta=0x%08" PRIx32
                                                  " elapsed=%.3fs addr=0x%02X drops=%" PRIu32 " — dropped",
                                             chip_idx, d, ghs, delta, (float)seconds, asic_addr,
                                             s_chip_meas[chip_idx].domain_drops[d]);
                                    s_chip_warn[chip_idx].domain_last_warn_us[d] = step.new_last_warn_us;
                                }
                            }
                        }
                    }
                    if (s_chip_meas[chip_idx].domain_samples[d] < 3) s_chip_meas[chip_idx].domain_samples[d]++;
                    s_chip_meas[chip_idx].domain_val[d] = value;
                    s_chip_meas[chip_idx].domain_time_us[d] = now_us;
                }
                continue;
            }

            // Decode job ID and look up
            uint8_t real_job_id = asic_decode_job_id(&nonce);
            if (real_job_id >= ASIC_JOB_ID_MOD) {
                bb_log_w(TAG, "nonce: real_job_id=%u out of wire range, dropped", real_job_id);
                continue;
            }
            size_t recv_slot = asic_job_slot(real_job_id);
            if (asic_job_slot_stale(s_job_id_seen[recv_slot],
                                    s_job_table[recv_slot].job_id[0],
                                    real_job_id)) {
                // Stale: the slot has been recycled for a different job ID, or was never written.
                bb_log_w(TAG, "nonce for unknown/stale job_id=%u (slot=%zu seen=%u), dropped",
                         real_job_id, recv_slot, s_job_id_seen[recv_slot]);
                continue;
            }

            mining_work_t *orig = &s_job_table[recv_slot];
            uint32_t ver_bits = asic_decode_version_bits(&nonce);
            uint32_t nonce_val = bb_load_be32(nonce.nonce);
            // LE uint32: raw ASIC wire bytes interpreted as little-endian (pool submission value)
            uint32_t nonce_le = bb_load_le32(nonce.nonce);

            // Dedup: skip if we already submitted this nonce+version for this job
            if (asic_nonce_dedup_check_and_insert(&s_dedup, real_job_id, nonce_val, ver_bits)) {
                continue;
            }

            nonces_since_log++;

            // Reconstruct the header with version rolling and nonce applied,
            // then SHA256d it — mirroring the logic formerly in asic_share_validate.
            uint8_t header_copy[80];
            memcpy(header_copy, orig->header, 80);
            if (ver_bits != 0) {
                uint32_t effective_mask = orig->version_mask ? orig->version_mask : ASIC_VERSION_MASK;
                uint32_t rolled = (orig->version & ~effective_mask) | (ver_bits & effective_mask);
                header_copy[0] = (uint8_t)(rolled);
                header_copy[1] = (uint8_t)(rolled >> 8);
                header_copy[2] = (uint8_t)(rolled >> 16);
                header_copy[3] = (uint8_t)(rolled >> 24);
            }
            header_copy[76] = (uint8_t)(nonce_le);
            header_copy[77] = (uint8_t)(nonce_le >> 8);
            header_copy[78] = (uint8_t)(nonce_le >> 16);
            header_copy[79] = (uint8_t)(nonce_le >> 24);

            uint8_t hash[32];
            sha256d(header_copy, 80, hash);

            double share_diff = 0.0;
            share_verdict_t verdict = share_validate(orig, hash, &share_diff);
            if (verdict == SHARE_BELOW_TARGET) {
                s_nonce_verify_fail++;
                continue;
            }
            s_nonce_verify_pass++;
            if (verdict == SHARE_INVALID_TARGET || verdict == SHARE_LOW_DIFFICULTY) {
                bb_log_e(TAG, "share sanity fail: share_diff=%.4f pool_diff=%.4f, skipping",
                         share_diff, orig->difficulty);
                continue;
            }

            int64_t now_ts;
            {
                time_t t = time(NULL);
                now_ts = ((int64_t)t < 1700000000LL) ? 0 : (int64_t)t;
            }
            if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                if (share_diff > mining_stats.session.best_diff) {
                    mining_stats.session.best_diff = share_diff;
                    mining_stats.session.best_diff_ts = now_ts;
                }
                mining_pool_stat_t *slot = stratum_active_pool_slot();
                if (slot && share_diff > slot->best_diff) {
                    slot->best_diff = share_diff;
                    slot->best_diff_ts = now_ts;
                }
                xSemaphoreGive(mining_stats.mutex);
            }

            // Block detection: check if this share meets the network target.
            if (share_meets_network_target(hash, orig->nbits)) {
                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    mining_stats.session.blocks_found++;
                    mining_stats.session.last_block_ts = now_ts;
                    xSemaphoreGive(mining_stats.mutex);
                }
                mining_pool_stat_t *slot = stratum_active_pool_slot();
                if (slot) {
                    mining_pool_stats_record_block(slot, now_ts);
                }
                bb_event_topic_t btopic = mining_pool_stats_get_block_topic();
                if (btopic) {
                    char payload[256];
                    const char *bhost = slot ? slot->host : "";
                    uint16_t bport = slot ? slot->port : 0;
                    snprintf(payload, sizeof(payload),
                        "{\"host\":\"%s\",\"port\":%u,\"share_diff\":%.4f,\"timestamp\":%lld}",
                        bhost, (unsigned)bport, share_diff, (long long)now_ts);
                    bb_event_post(btopic, 0, payload, strlen(payload));
                }
            }

            uint32_t effective_mask = orig->version_mask ? orig->version_mask : ASIC_VERSION_MASK;
            uint32_t rolled_ver = (ver_bits != 0)
                ? (orig->version & ~effective_mask) | (ver_bits & effective_mask)
                : orig->version;
            bb_log_i(DIAG, "share: job=%u ver=%08" PRIx32 " n=%02X%02X%02X%02X",
                     real_job_id, rolled_ver,
                     nonce.nonce[0], nonce.nonce[1], nonce.nonce[2], nonce.nonce[3]);

            mining_result_t result;
            package_result(&result, orig, nonce_le, ver_bits);
            result.share_diff = orig->difficulty;  // TA-344: pool-assigned diff at issue time

            if (!stratum_is_connected()) {
                bb_log_d(TAG, "stratum disconnected, discarding share");
            } else if (xQueueSend(result_queue, &result, 0) != pdTRUE) {
                bb_log_w(TAG, "result queue full, share dropped");
            }
        }

        // 4. Periodic temp reading + fan control (~every 5s).
        // bb_fan autofan (CONFIG_BB_FAN_AUTOFAN) owns the PID and manual duty inside
        // bb_fan_poll(). TM feeds VR aux temp before polling so the dual-EMA PID sees
        // both sensors. Emergency override: if die temp is NaN (sensor failed entirely),
        // TM forces 100% duty directly — BB autofan PID would compute with NaN input
        // which is undefined, so we must apply the failsafe before poll.
        TickType_t now = xTaskGetTickCount();
        if (now - last_temp_tick >= pdMS_TO_TICKS(5000)) {
            bb_fan_handle_t fan_h = bb_fan_primary();

#ifdef CONFIG_BB_FAN_AUTOFAN
            // Feed VR aux temp to BB autofan before poll so the PID sees it this tick.
            // Read last-known VR temp from mining_stats (populated by power poll below).
            if (fan_h != NULL) {
                float vr_temp_raw = -1.0f;
                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                    vr_temp_raw = mining_stats.vr_temp_c;
                    xSemaphoreGive(mining_stats.mutex);
                }
                // Pass negative to invalidate if VR sensor absent/failed
                bb_fan_set_aux_temp(fan_h, vr_temp_raw);
            }
#endif /* CONFIG_BB_FAN_AUTOFAN */

            // Poll fan HAL: bb_fan_poll refreshes snapshot AND runs autofan PID (if enabled).
            if (fan_h != NULL) {
                bb_fan_poll(fan_h);
            }
            bb_fan_snapshot_t fs;
            bb_fan_snapshot(fan_h, &fs);

            float temp = fs.die_c;
            bool temp_ok = !isnan(temp);

            if (temp_ok) {
                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                    mining_stats.asic_temp_c = temp;
                    xSemaphoreGive(mining_stats.mutex);
                }
            }

            // Safety override: if die temp read failed (NaN), force 100% duty.
            // BB autofan PID would receive NaN input — undefined. This must run
            // even with CONFIG_BB_FAN_AUTOFAN enabled. High-temp protection is
            // handled naturally by BB's REVERSE PID (output rises when die > target).
            if (!temp_ok && fan_h != NULL) {
                bb_fan_set_duty_pct(fan_h, 100);
            }

            {
                // Poll power HAL and gather snapshot
                bb_power_handle_t pwr_h = bb_power_primary();
                if (pwr_h != NULL) {
                    bb_power_poll(pwr_h);
                }
                bb_power_snapshot_t ps;
                bb_power_snapshot(pwr_h, &ps);

                int v   = ps.vout_mv;
                int i   = ps.iout_ma;
                int vin = ps.vin_mv;
                int p   = (v >= 0 && i >= 0)
                          ? (int)((int64_t)v * i / 1000) + BOARD_POWER_OFFSET_MW
                          : -1;

                // fan snapshot was already populated above (after poll)
                int rpm  = fs.rpm;
                int duty = fs.duty_pct;
                float board_t = isnan(fs.board_c) ? -1.0f : fs.board_c;

                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                    mining_stats.vcore_mv = v;
                    mining_stats.icore_ma = i;
                    mining_stats.pcore_mw = p;
                    mining_stats.vin_mv   = vin;
                    mining_stats.vr_temp_c = (ps.temp_c >= 0) ? (float)ps.temp_c : -1.0f;
                    mining_stats.fan_rpm = rpm;
                    mining_stats.fan_duty_pct = duty;
                    mining_stats.board_temp_c = board_t;

                    float pcore_val = (p >= 0) ? (float)p : NAN;
                    float pc_1m = 0.0f, pc_10m = 0.0f, pc_1h = 0.0f;
                    mining_avg_update(s_pcore_poll_count, pcore_val,
                                           s_pcore_1m, s_pcore_10m, s_pcore_1h,
                                           &s_pcore_10m_prev, &s_pcore_1h_prev,
                                           &pc_1m, &pc_10m, &pc_1h);
                    mining_stats.pcore_mw_1m = pc_1m;
                    mining_stats.pcore_mw_10m = pc_10m;
                    mining_stats.pcore_mw_1h = pc_1h;
                    s_pcore_poll_count++;
                    xSemaphoreGive(mining_stats.mutex);
                }

#ifdef CONFIG_TM_VCORE_WATCHDOG
                // TA-318: vcore-collapse watchdog — eval on every 5s power tick.
                // v = ps.vout_mv (vcore in mV); in scope from power snapshot above.
                {
                    bb_vcore_wd_input_t wd_in = {
                        .vcore_mv     = v,
                        .rail_enabled = true,  // ASIC_EN asserted; mining active post-init
                        .uptime_ms    = (uint64_t)(bb_timer_now_us() / 1000ULL),
                    };
                    bb_vcore_wd_action_t wd_act = bb_vcore_wd_eval(&s_vcore_wd, &wd_in);

                    if (wd_act == BB_VCORE_WD_RECOVER) {
                        s_vcore_restart_count++;
                        bb_nv_set_u32(VCORE_WD_NVS_NS, VCORE_WD_NVS_KEY, s_vcore_restart_count);
                        bb_log_e(TAG, "vcore collapsed to %d mV — in-place rail-cycle recovery (attempt %" PRIu32 ")",
                                 v, s_vcore_restart_count);
                        bb_err_t rec = asic_vcore_recover_inplace();
                        if (rec != BB_OK) {
                            bb_log_e(TAG, "in-place recovery failed (%d) — falling back to restart", rec);
                            esp_restart();
                        }
                    } else if (wd_act == BB_VCORE_WD_BACKOFF) {
                        // Burst exhausted — log once per backoff entry, do NOT restart.
                        if (!s_vcore_backoff_logged) {
                            bb_log_e(TAG, "vcore collapsed but auto-restart budget exhausted — holding (check power)");
                            s_vcore_backoff_logged = true;
                        }
                    } else {
                        // Healthy or warmup — clear backoff flag; clear NVS count when BB
                        // burst counter resets (sustained 300s healthy streak).
                        s_vcore_backoff_logged = false;
                        if (s_vcore_restart_count > 0 && s_vcore_wd.burst_count == 0) {
                            s_vcore_restart_count = 0;
                            bb_nv_set_u32(VCORE_WD_NVS_NS, VCORE_WD_NVS_KEY, 0);
                            bb_log_i(TAG, "vcore healthy for 300s — cleared restart count");
                        }
                    }
                }
#endif /* CONFIG_TM_VCORE_WATCHDOG */
            }

            last_temp_tick = now;
        }

        // Every 5s: poll total + error counters for per-chip HW telemetry (TA-192) and compute rolling averages (TA-196)
        if (now - last_reg_poll >= pdMS_TO_TICKS(ASIC_POLL_PERIOD_MS)) {
            for (size_t i = 0; i < sizeof(s_poll_regs); i++) {
                read_reg(s_poll_regs[i]);
                vTaskDelay(pdMS_TO_TICKS(10));
                mining_pause_check();
            }
            // Let responses settle before aggregating.
            vTaskDelay(pdMS_TO_TICKS(100));

            float total_sum = 0.0f, error_sum = 0.0f;
            for (int i = 0; i < BOARD_ASIC_COUNT; i++) {
                total_sum += s_chip_meas[i].total_ghs;
                error_sum += s_chip_meas[i].error_ghs;
            }
            float hw_err_pct = (total_sum > 0.001f) ? (error_sum / total_sum * 100.0f) : 0.0f;

            float t_1m = 0.0f, t_10m = 0.0f, t_1h = 0.0f;
            float h_1m = 0.0f, h_10m = 0.0f, h_1h = 0.0f;
            mining_avg_update(s_avg_poll_count, total_sum,
                                   s_total_1m, s_total_10m, s_total_1h,
                                   &s_total_10m_prev, &s_total_1h_prev,
                                   &t_1m, &t_10m, &t_1h);
            mining_avg_update(s_avg_poll_count, hw_err_pct,
                                   s_hw_err_1m, s_hw_err_10m, s_hw_err_1h,
                                   &s_hw_err_10m_prev, &s_hw_err_1h_prev,
                                   &h_1m, &h_10m, &h_1h);

            /* TA-363: pool-effective rolling sampler (ASIC reuses the same 5s poll counter) */
            double sum_now = 0.0;
            if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                sum_now = mining_stats.session.accepted_diff_sum;
                xSemaphoreGive(mining_stats.mutex);
            }
            float pe_1m = 0.0f, pe_10m = 0.0f, pe_1h = 0.0f;
            mining_pool_eff_tick(sum_now, (double)ASIC_POLL_PERIOD_MS / 1000.0,
                                 &s_pool_eff_prev_sum, s_pool_eff_poll_count,
                                 s_pool_eff_1m, s_pool_eff_10m, s_pool_eff_1h,
                                 &s_pool_eff_10m_prev, &s_pool_eff_1h_prev,
                                 &pe_1m, &pe_10m, &pe_1h);

            s_avg_poll_count++;
            s_pool_eff_poll_count++;

            if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                mining_stats.asic_total_ghs = total_sum;
                mining_stats.asic_hw_error_pct = hw_err_pct;
                mining_stats.asic_total_ghs_1m = t_1m;
                mining_stats.asic_total_ghs_10m = t_10m;
                mining_stats.asic_total_ghs_1h = t_1h;
                mining_stats.asic_hw_error_pct_1m = h_1m;
                mining_stats.asic_hw_error_pct_10m = h_10m;
                mining_stats.asic_hw_error_pct_1h = h_1h;
                mining_stats.pool_eff_1m  = pe_1m;
                mining_stats.pool_eff_10m = pe_10m;
                mining_stats.pool_eff_1h  = pe_1h;
                xSemaphoreGive(mining_stats.mutex);
            }

            last_reg_poll = now;
        }

        // 5. Periodic status log (~every 30s)
        if (now - last_hashrate_tick >= pdMS_TO_TICKS(30000)) {
            float elapsed_s = (float)(now - last_hashrate_tick) / (float)configTICK_RATE_HZ;
            // Each nonce = ASIC_TICKET_DIFF × 2^32 hashes
            double hashrate = (elapsed_s > 0) ? (double)nonces_since_log * ASIC_TICKET_DIFF * 4294967296.0 / elapsed_s : 0;
            uint32_t shares = 0;
            float temp = 0;
            float total_ghs_sum = 0.0f;
            float asic_hw_error_pct = 0.0f;
            if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                mining_stats.asic_hashrate = hashrate;
                mining_stats_update_ema(&mining_stats.asic_ema, hashrate, (int64_t)now * (1000000 / configTICK_RATE_HZ));
                mining_stats.hw_hashrate = hashrate;
                mining_stats_update_ema(&mining_stats.hw_ema, hashrate, (int64_t)now * (1000000 / configTICK_RATE_HZ));

                // Compute effective frequency from EMA
                if (mining_stats.asic_ema.value > 0) {
                    // hashrate EMA is in H/s — convert to MH/s, then divide by core count
                    double effective_mhz = (mining_stats.asic_ema.value / 1e6) / (BOARD_SMALL_CORES * BOARD_ASIC_COUNT);
                    mining_stats.asic_freq_effective_mhz = (float)effective_mhz;
                }

                // Read aggregated per-chip register telemetry (TA-192) — computed in 5s tick (TA-196)
                total_ghs_sum = mining_stats.asic_total_ghs;
                asic_hw_error_pct = mining_stats.asic_hw_error_pct;

                shares = mining_stats.asic_shares;
                temp = mining_stats.asic_temp_c;
                xSemaphoreGive(mining_stats.mutex);
            }

            mining_stats_sample_die_temp();  // ESP die temp into mining_stats.temp_c

            bb_log_i(TAG, "%.1f GH/s (reported %.1f) | hw_err: %.2f%% | temp: %.1f C | shares: %" PRIu32 " | nonce verify pass/fail: %" PRIu32 "/%" PRIu32,
                     hashrate / 1e9, total_ghs_sum, asic_hw_error_pct,
                     temp, shares, s_nonce_verify_pass, s_nonce_verify_fail);

            // TA-198: per-chip raw counters — diag only (601 vs 650 comparison)
            for (int c = 0; c < BOARD_ASIC_COUNT; c++) {
                bb_log_i(DIAG, "chip %d: total_raw=%" PRIu32 " error_raw=%" PRIu32
                              " total_ghs=%.1f error_ghs=%.2f",
                         c,
                         s_chip_meas[c].total_val - s_chip_meas[c].total_val_base,
                         s_chip_meas[c].error_val - s_chip_meas[c].error_val_base,
                         s_chip_meas[c].total_ghs, s_chip_meas[c].error_ghs);
            }

            // Record hashes for this 30s period to per-pool stats.
            // Each nonce represents ASIC_TICKET_DIFF * 2^32 candidate hashes.
            {
                uint64_t hashes_since_log = (uint64_t)((double)nonces_since_log * ASIC_TICKET_DIFF * 4294967296.0);
                mining_pool_stat_t *slot = stratum_active_pool_slot();
                if (slot) {
                    mining_pool_stats_record_hashes(slot, hashes_since_log);
                }
            }

            nonces_since_log = 0;
            last_hashrate_tick = now;
        }

        bb_wdt_task_feed();
    }
}

// --- Miner config ---
// Note: g_miner_config.roll_interval_ms uses chip-specific JOB_INTERVAL_MS from board.h
// Both BM1370 and BM1368 boards define their respective intervals
#ifdef ASIC_BM1370
#define ASIC_JOB_INTERVAL_MS BM1370_JOB_INTERVAL_MS
#else
#define ASIC_JOB_INTERVAL_MS BM1368_JOB_INTERVAL_MS
#endif
const miner_config_t g_miner_config = {
    .init = NULL,  // asic_init called separately before WiFi
    .task_fn = asic_mining_task,
    .name = "asic",
    .stack_size = 8192,
    .priority = 20,
    .core = 1,
    .extranonce2_roll = true,
    .roll_interval_ms = ASIC_JOB_INTERVAL_MS,
};

i2c_master_bus_handle_t asic_get_i2c_bus(void)
{
    return s_i2c_bus;
}

void asic_set_i2c_bus(i2c_master_bus_handle_t bus)
{
    s_i2c_bus = bus;
}

// TA-192 phase 2: Per-chip telemetry snapshot for /api/stats
int asic_task_get_chip_telemetry(asic_chip_telemetry_t *out, int max_chips)
{
    int n = (max_chips < BOARD_ASIC_COUNT) ? max_chips : BOARD_ASIC_COUNT;
    for (int c = 0; c < n; c++) {
        out[c].total_ghs = s_chip_meas[c].total_ghs;
        out[c].error_ghs = s_chip_meas[c].error_ghs;
        out[c].hw_err_pct = (s_chip_meas[c].total_ghs > 0.001f)
            ? (s_chip_meas[c].error_ghs / s_chip_meas[c].total_ghs * 100.0f)
            : 0.0f;
        for (int d = 0; d < 4; d++) {
            out[c].domain_ghs[d] = s_chip_meas[c].domain_ghs[d];
        }
        // Baseline-subtracted: excludes counter activity from PLL ramp.
        out[c].total_raw = s_chip_meas[c].total_val - s_chip_meas[c].total_val_base;
        out[c].error_raw = s_chip_meas[c].error_val - s_chip_meas[c].error_val_base;
        // TA-223: drop counters
        out[c].total_drops = s_chip_meas[c].total_drops;
        out[c].error_drops = s_chip_meas[c].error_drops;
        for (int d = 0; d < 4; d++) {
            out[c].domain_drops[d] = s_chip_meas[c].domain_drops[d];
        }
        // TA-237
        out[c].last_drop_us = s_chip_meas[c].last_drop_us;
    }
    return n;
}

size_t asic_task_get_drop_log(asic_drop_event_t *out, size_t max_out)
{
    return asic_drop_log_snapshot(&s_drop_log, out, max_out);
}

#endif // ASIC_BM1370 || ASIC_BM1368
