#ifdef ASIC_BM1370

#include "asic.h"
#include "asic_proto.h"
#include "asic_internal.h"
#include "bm1370.h"
#include "tps546.h"
#include "pll.h"
#include "board.h"
#include "mining.h"
#include "esp_log.h"
#include "bb_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bm1370";

// --- Set PLL frequency ---
static void set_pll_freq(float freq_mhz)
{
    pll_params_t pll;
    pll_calc(freq_mhz, BM1370_FB_MIN, BM1370_FB_MAX, &pll);
    uint8_t vdo = pll_vdo_scale(&pll);
    uint8_t postdiv = pll_postdiv_byte(&pll);
    write_reg(BM1370_REG_PLL, vdo, (uint8_t)pll.fb_div, pll.refdiv, postdiv);
    bb_log_d(TAG, "PLL: target=%.1f actual=%.1f fb=%u ref=%u p1=%u p2=%u",
             freq_mhz, pll.actual_mhz, pll.fb_div, pll.refdiv, pll.post1, pll.post2);
}

// --- BM1370 chip init sequence ---
static esp_err_t bm1370_chip_init(void)
{
    uint16_t addr_interval = 256 / BM1370_CHIP_COUNT;

    // Step 1: Set version mask x3
    for (int i = 0; i < 3; i++) {
        write_reg(BM1370_REG_VERSION, 0x90, 0x00, 0xFF, 0xFF);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Step 2: Read chip ID (broadcast)
    uint8_t read_data[2] = {0x00, BM1370_REG_CHIP_ID};
    send_cmd(ASIC_CMD_READ, ASIC_GROUP_ALL, read_data, 2);

    // Wait and read chip responses
    int chip_count = 0;
    for (int i = 0; i < BM1370_CHIP_COUNT + 1; i++) {
        uint8_t rx[ASIC_NONCE_LEN];
        int n = asic_uart_read(rx, ASIC_NONCE_LEN, 1000);
        if (n != ASIC_NONCE_LEN) break;
        if (rx[0] != ASIC_PREAMBLE_RX_0 || rx[1] != ASIC_PREAMBLE_RX_1) break;
        // Check chip ID in response bytes 2-3 (big-endian)
        uint16_t chip_id = ((uint16_t)rx[2] << 8) | rx[3];
        bb_log_i(TAG, "chip %d: ID=0x%04X", chip_count, chip_id);
        chip_count++;
    }

    if (chip_count == 0) {
        bb_log_e(TAG, "no BM1370 chips detected");
        return ESP_ERR_NOT_FOUND;
    }
    bb_log_i(TAG, "detected %d chip(s)", chip_count);

    // Step 3: Version mask x1 more
    write_reg(BM1370_REG_VERSION, 0x90, 0x00, 0xFF, 0xFF);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Step 4: Reg A8 broadcast
    write_reg(BM1370_REG_A8, 0x00, 0x07, 0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 5: MISC_CTRL broadcast
    write_reg(BM1370_REG_MISC_CTRL, 0xF0, 0x00, 0xC1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 6: Chain inactive
    uint8_t inactive_data[2] = {0x00, 0x00};
    send_cmd(ASIC_CMD_INACTIVE, ASIC_GROUP_ALL, inactive_data, 2);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 7: Address assignment
    for (int i = 0; i < chip_count; i++) {
        uint8_t addr_data[2] = {(uint8_t)(i * addr_interval), 0x00};
        send_cmd(ASIC_CMD_SETADDR, ASIC_GROUP_SINGLE, addr_data, 2);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Step 8: Core register control (broadcast)
    write_reg(BM1370_REG_CORE_CTRL, 0x80, 0x00, 0x8B, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(BM1370_REG_CORE_CTRL, 0x80, 0x00, 0x80, 0x0C);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 9: Ticket mask placeholder — re-written after baud switch + freq ramp (step 16)
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 10: IO driver strength
    write_reg(BM1370_REG_IO_DRV, 0x00, 0x01, 0x11, 0x11);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 11: Per-chip register writes
    for (int i = 0; i < chip_count; i++) {
        uint8_t addr = (uint8_t)(i * addr_interval);
        write_reg_chip(addr, BM1370_REG_A8, 0x00, 0x07, 0x01, 0xF0);
        write_reg_chip(addr, BM1370_REG_MISC_CTRL, 0xF0, 0x00, 0xC1, 0x00);
        write_reg_chip(addr, BM1370_REG_CORE_CTRL, 0x80, 0x00, 0x8B, 0x00);
        write_reg_chip(addr, BM1370_REG_CORE_CTRL, 0x80, 0x00, 0x80, 0x0C);
        write_reg_chip(addr, BM1370_REG_CORE_CTRL, 0x80, 0x00, 0x82, 0xAA);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Step 12: Misc registers
    write_reg(BM1370_REG_MISC_SET, 0x00, 0x00, 0x44, 0x80);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(BM1370_REG_ANALOG_MUX, 0x00, 0x00, 0x00, 0x02);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(BM1370_REG_MISC_SET, 0x00, 0x00, 0x44, 0x80);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(BM1370_REG_CORE_CTRL, 0x80, 0x00, 0x8D, 0xEE);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Frequency ramp from 6.25 to target — must run at the initial 115200 baud.
    // The ASIC's UART clock divider is derived from its PLL; switching to 1 Mbps
    // before the PLL settles at target yields a malformed TX eye and UART errors
    // at higher frequencies. See TA-190.
    float target_freq = (float)BM1370_DEFAULT_FREQ_MHZ;
    bb_log_i(TAG, "ramping frequency to %.1f MHz", target_freq);
    for (float freq = 6.25f; freq <= target_freq; freq += 6.25f) {
        set_pll_freq(freq);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    set_pll_freq(target_freq);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Baud switch to 1 Mbps — only after PLL is stable at target.
    write_reg(BM1370_REG_FAST_UART, 0x11, 0x30, 0x02, 0x00);
    uart_wait_tx_done(ASIC_UART_NUM, pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(uart_set_baudrate(ASIC_UART_NUM, ASIC_BAUD_FAST));
    uart_flush(ASIC_UART_NUM);
    bb_log_i(TAG, "baud switched to %d", ASIC_BAUD_FAST);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Step 15: Hash counting register
    write_reg(BM1370_REG_HASH_COUNT, 0x00, 0x00, 0x1E, 0xB5);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 16: Ticket mask — written last to avoid reset by baud switch or freq ramp
    set_ticket_mask(ASIC_TICKET_DIFF);
    vTaskDelay(pdMS_TO_TICKS(5));

    bb_log_i(TAG, "BM1370 init complete");
    return ESP_OK;
}

// --- Voltage regulator wrapper ---
static esp_err_t tps546_vreg_init(i2c_master_bus_handle_t bus, uint16_t target_mv)
{
    return tps546_init(bus, TPS546_I2C_ADDR, target_mv);
}

// --- Ops struct and global ---
static const asic_chip_ops_t s_bm1370_ops = {
    .chip_init       = bm1370_chip_init,
    .vreg_init       = tps546_vreg_init,
    .fb_min          = BM1370_FB_MIN,
    .fb_max          = BM1370_FB_MAX,
    .default_mv      = BM1370_DEFAULT_MV,
    .default_freq_mhz = BM1370_DEFAULT_FREQ_MHZ,
    .chip_count      = BM1370_CHIP_COUNT,
    .chip_id         = BM1370_CHIP_ID,
    .job_interval_ms = BM1370_JOB_INTERVAL_MS,
};

const asic_chip_ops_t *g_chip_ops = &s_bm1370_ops;

#endif // ASIC_BM1370
