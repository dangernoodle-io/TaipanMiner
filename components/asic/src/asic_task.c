#include "asic_chip.h"
#ifdef ASIC_CHIP

#include "asic.h"
#include "asic_proto.h"
#include "asic_internal.h"
#include "crc.h"
#include "tps546.h"
#include "emc2101.h"
#include "emc2101_curve.h"
#include "mining.h"
#include "stratum.h"
#include "work.h"
#include "sha256.h"
#include "board.h"
#include "board_gpio.h"
#include "bm1370.h"

#include "esp_log.h"
#include "bb_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/temperature_sensor.h"
#include "esp_task_wdt.h"

#include <string.h>
#include <inttypes.h>
#include <math.h>

static const char *TAG = "asic";

// --- I2C bus handle (initialized in asic_init, shared with display) ---
static i2c_master_bus_handle_t s_i2c_bus;

// --- Active job table (static to avoid stack allocation ~28 KB) ---
static mining_work_t s_job_table[ASIC_JOB_ID_MOD];
static uint8_t s_next_job_id;
static uint32_t s_current_work_seq;
static double s_current_asic_diff = 0;

// Fixed ASIC ticket mask difficulty — ASIC produces zero nonces above ~512.
// Local SHA256d checks each nonce against pool target before submission.
#define ASIC_TICKET_DIFF 256.0

// --- Nonce dedup (ASIC loops nonce space quickly, reports same results) ---
#define DEDUP_SIZE 16
static struct { uint8_t job_id; uint32_t nonce; uint32_t ver; } s_dedup[DEDUP_SIZE];
static uint8_t s_dedup_idx;

// Debug counters for SHA256d filter
static uint32_t s_sha_pass;
static uint32_t s_sha_fail;

// TA-192: per-chip register-derived telemetry (polled every 5s)
static struct {
    uint32_t total_val;        // last REG_TOTAL_COUNT read
    uint32_t error_val;        // last REG_ERROR_COUNT read
    uint64_t total_time_us;    // esp_timer_get_time() at last total_val
    uint64_t error_time_us;
    float    total_ghs;
    float    error_ghs;
    bool     total_init;
    bool     error_init;
} s_chip_meas[BOARD_ASIC_COUNT];

// TA-196: rolling-window averages mirroring ESP-Miner's hashrate_monitor_task.c
#define ASIC_POLL_PERIOD_MS 5000
#define ASIC_AVG_1M_SIZE    12  // 12 * 5s = 60s
#define ASIC_AVG_10M_SIZE   10
#define ASIC_AVG_1H_SIZE    6
#define ASIC_AVG_DIV_10M    ASIC_AVG_1M_SIZE
#define ASIC_AVG_DIV_1H     (ASIC_AVG_10M_SIZE * ASIC_AVG_DIV_10M)

static unsigned long s_avg_poll_count;
static float s_total_1m[ASIC_AVG_1M_SIZE];
static float s_total_10m[ASIC_AVG_10M_SIZE];
static float s_total_1h[ASIC_AVG_1H_SIZE];
static float s_total_10m_prev, s_total_1h_prev;
static float s_hw_err_1m[ASIC_AVG_1M_SIZE];
static float s_hw_err_10m[ASIC_AVG_10M_SIZE];
static float s_hw_err_1h[ASIC_AVG_1H_SIZE];
static float s_hw_err_10m_prev, s_hw_err_1h_prev;

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

// TA-196: NaN-safe averaging helpers
static float avg_nan_safe(const float *buf, size_t n)
{
    float sum = 0.0f;
    int count = 0;
    for (size_t i = 0; i < n; i++) {
        if (!isnanf(buf[i])) {
            sum += buf[i];
            count++;
        }
    }
    return count > 0 ? sum / (float)count : 0.0f;
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
}

// Progressive-blend averaging — matches ESP-Miner's update_hashrate_averages.
// Does NOT increment s_avg_poll_count — caller does after both metrics update.
static void update_metric_avg(float sample,
                              float *buf_1m, float *buf_10m, float *buf_1h,
                              float *prev_10m, float *prev_1h,
                              float *out_1m, float *out_10m, float *out_1h)
{
    unsigned long pc = s_avg_poll_count;

    buf_1m[pc % ASIC_AVG_1M_SIZE] = sample;
    *out_1m = avg_nan_safe(buf_1m, ASIC_AVG_1M_SIZE);

    int blend_10m = pc % ASIC_AVG_1M_SIZE;
    if (blend_10m == 0) {
        *prev_10m = buf_10m[(pc / ASIC_AVG_DIV_10M) % ASIC_AVG_10M_SIZE];
    }
    float v_10m = *out_1m;
    if (!isnanf(*prev_10m)) {
        float f = (blend_10m + 1.0f) / (float)ASIC_AVG_1M_SIZE;
        v_10m = f * v_10m + (1.0f - f) * (*prev_10m);
    }
    buf_10m[(pc / ASIC_AVG_DIV_10M) % ASIC_AVG_10M_SIZE] = v_10m;
    *out_10m = avg_nan_safe(buf_10m, ASIC_AVG_10M_SIZE);

    int blend_1h = pc % ASIC_AVG_DIV_1H;
    if (blend_1h == 0) {
        *prev_1h = buf_1h[(pc / ASIC_AVG_DIV_1H) % ASIC_AVG_1H_SIZE];
    }
    float v_1h = *out_10m;
    if (!isnanf(*prev_1h)) {
        float f = (blend_1h + 1.0f) / (float)ASIC_AVG_DIV_1H;
        v_1h = f * v_1h + (1.0f - f) * (*prev_1h);
    }
    buf_1h[(pc / ASIC_AVG_DIV_1H) % ASIC_AVG_1H_SIZE] = v_1h;
    *out_1h = avg_nan_safe(buf_1h, ASIC_AVG_1H_SIZE);
}

// --- asic_init ---
bb_err_t asic_init(void)
{
    bb_log_i(TAG, "initializing ASIC subsystem");

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

    // 2. GPIO: power enable + reset
    gpio_set_direction(PIN_ASIC_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_ASIC_EN, 1);
    bb_log_i(TAG, "ASIC EN=1 (power on)");

    gpio_set_direction(PIN_ASIC_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_ASIC_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_ASIC_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    bb_log_i(TAG, "ASIC reset complete");

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

    // 5. EMC2101 temp sensor + fan
    if (s_i2c_bus != NULL) {
        ESP_RETURN_ON_ERROR(emc2101_init(s_i2c_bus, EMC2101_I2C_ADDR), TAG, "emc2101");
        ESP_RETURN_ON_ERROR(emc2101_set_fan_duty(63), TAG, "fan on");  // max
    } else {
        bb_log_w(TAG, "I2C bus not available, skipping EMC2101 init");
    }
#else
    bb_log_w(TAG, "HAS_I2C undefined, skipping I2C init");
#endif

    // 6. Chip init sequence
    ESP_RETURN_ON_ERROR(g_chip_ops->chip_init(), TAG, "chip init");

    // Initialize state
    s_next_job_id = 0;
    memset(s_job_table, 0, sizeof(s_job_table));
    init_avg_buffers();

    bb_log_i(TAG, "ASIC subsystem ready");
    return BB_OK;
}

// --- Mining task ---
void asic_mining_task(void *arg)
{
    bb_log_i(TAG, "ASIC mining task started");
    esp_task_wdt_add(NULL);

    mining_work_t work;
    TickType_t last_temp_tick = 0;
    TickType_t last_hashrate_tick = xTaskGetTickCount();
    TickType_t last_reg_poll = 0;
    uint32_t nonces_since_log = 0;

    for (;;) {
        mining_pause_check();

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
            // Clean job: invalidate all active slots
            if (work.clean) {
                memset(s_job_table, 0, sizeof(s_job_table));
            }
            memset(s_dedup, 0, sizeof(s_dedup));
            s_dedup_idx = 0;

            // Cycle job ID
            s_next_job_id = (s_next_job_id + ASIC_JOB_ID_STEP) % ASIC_JOB_ID_MOD;

            // Extract and send job
            asic_job_t job;
            asic_extract_job(work.header, s_next_job_id, &job);

            uint8_t pkt[ASIC_JOB_PKT_LEN];
            asic_build_job(pkt, sizeof(pkt), &job);
            asic_uart_write(pkt, ASIC_JOB_PKT_LEN);

            // Store in job table
            memcpy(&s_job_table[s_next_job_id], &work, sizeof(work));
            s_current_work_seq = work.work_seq;

            if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                mining_stats.pool_difficulty = work.difficulty;
                xSemaphoreGive(mining_stats.mutex);
            }

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
                uint32_t value = ((uint32_t)rx[2] << 24) | ((uint32_t)rx[3] << 16) |
                                 ((uint32_t)rx[4] << 8)  | (uint32_t)rx[5];
                uint8_t asic_addr = rx[6];
                uint8_t reg_addr  = rx[7];

                uint8_t addr_interval = 256 / BOARD_ASIC_COUNT;
                int chip_idx = asic_addr / addr_interval;
                if (chip_idx < 0 || chip_idx >= BOARD_ASIC_COUNT) {
                    continue;
                }

                uint64_t now_us = esp_timer_get_time();
                static const uint64_t HASH_CNT_LSB = 1ULL << 32;

                if (reg_addr == BM1370_REG_TOTAL_COUNT) {
                    if (s_chip_meas[chip_idx].total_init) {
                        uint32_t delta = value - s_chip_meas[chip_idx].total_val;
                        float seconds = (float)(now_us - s_chip_meas[chip_idx].total_time_us) / 1e6f;
                        if (seconds > 0.001f) {
                            s_chip_meas[chip_idx].total_ghs =
                                (float)delta * (float)HASH_CNT_LSB / seconds / 1e9f;
                        }
                    } else {
                        s_chip_meas[chip_idx].total_init = true;
                    }
                    s_chip_meas[chip_idx].total_val = value;
                    s_chip_meas[chip_idx].total_time_us = now_us;
                } else if (reg_addr == BM1370_REG_ERROR_COUNT) {
                    if (s_chip_meas[chip_idx].error_init) {
                        uint32_t delta = value - s_chip_meas[chip_idx].error_val;
                        float seconds = (float)(now_us - s_chip_meas[chip_idx].error_time_us) / 1e6f;
                        if (seconds > 0.001f) {
                            s_chip_meas[chip_idx].error_ghs =
                                (float)delta * (float)HASH_CNT_LSB / seconds / 1e9f;
                        }
                    } else {
                        s_chip_meas[chip_idx].error_init = true;
                    }
                    s_chip_meas[chip_idx].error_val = value;
                    s_chip_meas[chip_idx].error_time_us = now_us;
                }
                continue;
            }

            // Decode job ID and look up
            uint8_t real_job_id = asic_decode_job_id(&nonce);
            if (real_job_id >= ASIC_JOB_ID_MOD || s_job_table[real_job_id].job_id[0] == '\0') {
                bb_log_w(TAG, "nonce for unknown job_id=%u", real_job_id);
                continue;
            }

            mining_work_t *orig = &s_job_table[real_job_id];
            uint32_t ver_bits = asic_decode_version_bits(&nonce);
            uint32_t nonce_val = ((uint32_t)nonce.nonce[0] << 24) | ((uint32_t)nonce.nonce[1] << 16) |
                                 ((uint32_t)nonce.nonce[2] << 8) | nonce.nonce[3];

            // Dedup: skip if we already submitted this nonce+version for this job
            bool dup = false;
            for (int d = 0; d < DEDUP_SIZE; d++) {
                if (s_dedup[d].job_id == real_job_id && s_dedup[d].nonce == nonce_val && s_dedup[d].ver == ver_bits) {
                    dup = true;
                    break;
                }
            }
            if (dup) {
                continue;
            }
            s_dedup[s_dedup_idx] = (typeof(s_dedup[0])){real_job_id, nonce_val, ver_bits};
            s_dedup_idx = (s_dedup_idx + 1) % DEDUP_SIZE;

            nonces_since_log++;

            // Local SHA256d verification — only submit nonces meeting pool target
            uint8_t header_copy[80];
            memcpy(header_copy, orig->header, 80);

            // Apply version rolling (LE in header)
            if (ver_bits != 0 && orig->version_mask != 0) {
                uint32_t rolled = (orig->version & ~orig->version_mask) | (ver_bits & orig->version_mask);
                header_copy[0] = (uint8_t)(rolled);
                header_copy[1] = (uint8_t)(rolled >> 8);
                header_copy[2] = (uint8_t)(rolled >> 16);
                header_copy[3] = (uint8_t)(rolled >> 24);
            }

            // Apply nonce — raw ASIC wire bytes map directly to header bytes
            // (submitted nonce is LE interpretation of wire bytes; pool writes LE back to header)
            header_copy[76] = nonce.nonce[0];
            header_copy[77] = nonce.nonce[1];
            header_copy[78] = nonce.nonce[2];
            header_copy[79] = nonce.nonce[3];

            uint8_t hash[32];
            sha256d(header_copy, 80, hash);

            if (!meets_target(hash, orig->target)) {
                s_sha_fail++;
                continue;
            }
            s_sha_pass++;

            double share_diff = hash_to_difficulty(hash);

            // Sanity check: target/difficulty must be valid and share must meet pool diff
            if (orig->difficulty < 0.001 || !is_target_valid(orig->target) ||
                share_diff < orig->difficulty * 0.5) {
                bb_log_e(TAG, "share sanity fail: share_diff=%.4f pool_diff=%.4f, skipping",
                         share_diff, orig->difficulty);
                continue;
            }

            if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                if (share_diff > mining_stats.session.best_diff) {
                    mining_stats.session.best_diff = share_diff;
                }
                xSemaphoreGive(mining_stats.mutex);
            }

            uint32_t rolled_ver = (ver_bits != 0 && orig->version_mask != 0)
                ? (orig->version & ~orig->version_mask) | (ver_bits & orig->version_mask)
                : orig->version;
            bb_log_i(TAG, "share: job=%u ver=%08" PRIx32 " n=%02X%02X%02X%02X",
                     real_job_id, rolled_ver,
                     nonce.nonce[0], nonce.nonce[1], nonce.nonce[2], nonce.nonce[3]);

            // Build result for stratum
            mining_result_t result;
            memset(&result, 0, sizeof(result));
            strncpy(result.job_id, orig->job_id, sizeof(result.job_id) - 1);
            strncpy(result.extranonce2_hex, orig->extranonce2_hex, sizeof(result.extranonce2_hex) - 1);

            // ntime from original work
            snprintf(result.ntime_hex, sizeof(result.ntime_hex), "%08" PRIx32, orig->ntime);

            // nonce — submit raw UART bytes as LE uint32 (matches ESP-Miner packed struct on LE ESP32)
            uint32_t nonce_le = nonce.nonce[0] | ((uint32_t)nonce.nonce[1] << 8) |
                                ((uint32_t)nonce.nonce[2] << 16) | ((uint32_t)nonce.nonce[3] << 24);
            snprintf(result.nonce_hex, sizeof(result.nonce_hex), "%08" PRIx32, nonce_le);

            // Version rolling — submit ver_bits (pool XORs with base version)
            if (ver_bits != 0 && orig->version_mask != 0) {
                snprintf(result.version_hex, sizeof(result.version_hex), "%08" PRIx32, ver_bits);
            }

            if (!stratum_is_connected()) {
                bb_log_d(TAG, "stratum disconnected, discarding share");
            } else if (xQueueSend(result_queue, &result, 0) != pdTRUE) {
                bb_log_w(TAG, "result queue full, share dropped");
            }
        }

        // 4. Periodic temp reading + fan control (~every 5s, log every 30s)
        TickType_t now = xTaskGetTickCount();
        if (now - last_temp_tick >= pdMS_TO_TICKS(5000)) {
            float temp;
            int new_duty;
            if (emc2101_read_temp(&temp) == ESP_OK) {
                new_duty = emc2101_duty_for_temp_c(temp);
                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                    mining_stats.asic_temp_c = temp;
                    xSemaphoreGive(mining_stats.mutex);
                }
            } else {
                // temp read failed — fail-safe to max cooling
                new_duty = 100;
            }
            emc2101_set_duty_pct(new_duty);

            {
                int v = tps546_read_vout_mv();
                int i = tps546_read_iout_ma();
                int p = (v >= 0 && i >= 0) ? (int)((int64_t)v * i / 1000) + BOARD_POWER_OFFSET_MW : -1;
                int vin = tps546_read_vin_mv();
                int vr_temp = tps546_read_temp_c();
                int rpm = emc2101_read_rpm();
                int duty = emc2101_get_duty_pct();
                float board_t = -1.0f;
                if (emc2101_read_internal_temp(&board_t) != ESP_OK) board_t = -1.0f;
                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                    mining_stats.vcore_mv = v;
                    mining_stats.icore_ma = i;
                    mining_stats.pcore_mw = p;
                    mining_stats.vin_mv = vin;
                    mining_stats.vr_temp_c = (vr_temp >= 0) ? (float)vr_temp : -1.0f;
                    mining_stats.fan_rpm = rpm;
                    mining_stats.fan_duty_pct = duty;
                    mining_stats.board_temp_c = board_t;
                    xSemaphoreGive(mining_stats.mutex);
                }
            }

            last_temp_tick = now;
        }

        // Every 5s: poll total + error counters for per-chip HW telemetry (TA-192) and compute rolling averages (TA-196)
        if (now - last_reg_poll >= pdMS_TO_TICKS(ASIC_POLL_PERIOD_MS)) {
            read_reg(BM1370_REG_TOTAL_COUNT);
            vTaskDelay(pdMS_TO_TICKS(10));
            read_reg(BM1370_REG_ERROR_COUNT);
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
            update_metric_avg(total_sum,
                              s_total_1m, s_total_10m, s_total_1h,
                              &s_total_10m_prev, &s_total_1h_prev,
                              &t_1m, &t_10m, &t_1h);
            update_metric_avg(hw_err_pct,
                              s_hw_err_1m, s_hw_err_10m, s_hw_err_1h,
                              &s_hw_err_10m_prev, &s_hw_err_1h_prev,
                              &h_1m, &h_10m, &h_1h);
            s_avg_poll_count++;

            if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                mining_stats.asic_total_ghs = total_sum;
                mining_stats.asic_hw_error_pct = hw_err_pct;
                mining_stats.asic_total_ghs_1m = t_1m;
                mining_stats.asic_total_ghs_10m = t_10m;
                mining_stats.asic_total_ghs_1h = t_1h;
                mining_stats.asic_hw_error_pct_1m = h_1m;
                mining_stats.asic_hw_error_pct_10m = h_10m;
                mining_stats.asic_hw_error_pct_1h = h_1h;
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

            // Read ESP die temp
            {
                float esp_temp = 0;
                temperature_sensor_handle_t th = mining_stats_temp_handle();
                if (th && temperature_sensor_get_celsius(th, &esp_temp) == ESP_OK) {
                    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                        mining_stats.temp_c = esp_temp;
                        xSemaphoreGive(mining_stats.mutex);
                    }
                }
            }

            bb_log_i(TAG, "asic: %.1f GH/s (reported %.1f) | hw_err: %.2f%% | temp: %.1f C | shares: %" PRIu32 " | sha pass/fail: %" PRIu32 "/%" PRIu32,
                     hashrate / 1e9, total_ghs_sum, asic_hw_error_pct,
                     temp, shares, s_sha_pass, s_sha_fail);
            nonces_since_log = 0;
            last_hashrate_tick = now;
        }

        esp_task_wdt_reset();
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

#endif // ASIC_BM1370 || ASIC_BM1368
