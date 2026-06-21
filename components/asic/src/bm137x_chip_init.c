#include "asic_chip.h"
#ifdef ASIC_CHIP

#include "bm137x_regs.h"
#include "asic.h"
#include "asic_proto.h"
#include "asic_internal.h"
#include "pll.h"
#include "board.h"
#include "esp_log.h"
#include "bb_log.h"
#include "bb_system.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Shared register addresses (identical on BM1370 and BM1368)
#define REG_CHIP_ID    0x00
#define REG_PLL        0x08
#define REG_HASH_COUNT 0x10
#define REG_MISC_CTRL  0x18
#define REG_FAST_UART  0x28
#define REG_CORE_CTRL  0x3C
#define REG_ANALOG_MUX 0x54
#define REG_IO_DRV     0x58
#define REG_VERSION    0xA4
#define REG_A8         0xA8
#define REG_MISC_SET   0xB9

static void bm137x_set_pll_freq(const bm137x_regs_t *r, float freq_mhz)
{
    pll_params_t pll;
    pll_calc(freq_mhz, r->fb_min, r->fb_max, &pll);
    uint8_t vdo = pll_vdo_scale(&pll);
    uint8_t postdiv = pll_postdiv_byte(&pll);
    write_reg(REG_PLL, vdo, (uint8_t)pll.fb_div, pll.refdiv, postdiv);
    bb_log_d(r->tag, "PLL: target=%.1f actual=%.1f fb=%u ref=%u p1=%u p2=%u",
             freq_mhz, pll.actual_mhz, pll.fb_div, pll.refdiv, pll.post1, pll.post2);
}

static void do_freq_ramp(const bm137x_regs_t *r, float target_freq_mhz)
{
    bb_log_i(r->tag, "ramping frequency to %.1f MHz", target_freq_mhz);
    for (float freq = 6.25f; freq <= target_freq_mhz; freq += 6.25f) {
        bm137x_set_pll_freq(r, freq);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    bm137x_set_pll_freq(r, target_freq_mhz);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void do_baud_switch(const bm137x_regs_t *r)
{
    write_reg(REG_FAST_UART, 0x11, 0x30, 0x02, 0x00);
    uart_wait_tx_done(ASIC_UART_NUM, pdMS_TO_TICKS(100));
    BB_ERROR_CHECK(uart_set_baudrate(ASIC_UART_NUM, ASIC_BAUD_FAST));
    uart_flush(ASIC_UART_NUM);
    bb_log_i(r->tag, "baud switched to %d", ASIC_BAUD_FAST);
    vTaskDelay(pdMS_TO_TICKS(10));
}

bb_err_t bm137x_chip_init(const bm137x_regs_t *r, float target_freq_mhz, int max_chips)
{
    // Chip detection must run at the chips' power-on default baud. A reboot re-inits the UART
    // at ASIC_BAUD_INIT, but in-place recovery reuses the port still at ASIC_BAUD_FAST from the
    // prior init's do_baud_switch — after a rail-cycle the chips reset to default baud, so
    // without this reset detection reads nothing ("no chips detected"). Idempotent on cold boot.
    BB_ERROR_CHECK(uart_set_baudrate(ASIC_UART_NUM, ASIC_BAUD_INIT));
    uart_flush(ASIC_UART_NUM);

    // Step 1: version mask x3
    for (int i = 0; i < 3; i++) {
        write_reg(REG_VERSION, 0x90, 0x00, 0xFF, 0xFF);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Step 2: read chip ID (broadcast)
    uint8_t read_data[2] = {0x00, REG_CHIP_ID};
    send_cmd(ASIC_CMD_READ, ASIC_GROUP_ALL, read_data, 2);

    int chip_count = 0;
    for (int i = 0; i < max_chips + 1; i++) {
        uint8_t rx[ASIC_NONCE_LEN];
        int n = asic_uart_read(rx, ASIC_NONCE_LEN, 1000);
        if (n != ASIC_NONCE_LEN) break;
        if (rx[0] != ASIC_PREAMBLE_RX_0 || rx[1] != ASIC_PREAMBLE_RX_1) break;
        uint16_t chip_id = ((uint16_t)rx[2] << 8) | rx[3];
        bb_log_i(r->tag, "chip %d: ID=0x%04X", chip_count, chip_id);
        chip_count++;
    }

    if (chip_count == 0) {
        bb_log_e(r->tag, "no %s chips detected", r->tag);
        return ESP_ERR_NOT_FOUND;
    }
    bb_log_i(r->tag, "detected %d chip(s)", chip_count);

    uint16_t addr_interval = r->addr_interval_from_detected
        ? (uint16_t)(256 / chip_count)
        : (uint16_t)(256 / r->nominal_chip_count);

    // Step 3: version mask x1 more
    write_reg(REG_VERSION, 0x90, 0x00, 0xFF, 0xFF);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Step 4: reg A8 broadcast
    write_reg(REG_A8, 0x00, 0x07, 0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 5: MISC_CTRL broadcast
    write_reg(REG_MISC_CTRL,
              r->misc_ctrl_b[0], r->misc_ctrl_b[1],
              r->misc_ctrl_b[2], r->misc_ctrl_b[3]);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 6: chain inactive
    uint8_t inactive_data[2] = {0x00, 0x00};
    send_cmd(ASIC_CMD_INACTIVE, ASIC_GROUP_ALL, inactive_data, 2);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 7: address assignment
    for (int i = 0; i < chip_count; i++) {
        uint8_t addr_data[2] = {(uint8_t)(i * addr_interval), 0x00};
        send_cmd(ASIC_CMD_SETADDR, ASIC_GROUP_SINGLE, addr_data, 2);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Step 8: core register control (broadcast)
    write_reg(REG_CORE_CTRL, 0x80, 0x00, 0x8B, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(REG_CORE_CTRL, 0x80, 0x00, 0x80, r->core_ctrl2_b3);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 9: ticket mask placeholder (written after baud/freq settle)
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 10: IO driver strength
    write_reg(REG_IO_DRV,
              r->io_drv_b[0], r->io_drv_b[1],
              r->io_drv_b[2], r->io_drv_b[3]);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 11: per-chip register writes
    for (int i = 0; i < chip_count; i++) {
        uint8_t addr = (uint8_t)(i * addr_interval);
        write_reg_chip(addr, REG_A8, 0x00, 0x07, 0x01, 0xF0);
        write_reg_chip(addr, REG_MISC_CTRL, 0xF0, 0x00, 0xC1, 0x00);
        write_reg_chip(addr, REG_CORE_CTRL, 0x80, 0x00, 0x8B, 0x00);
        write_reg_chip(addr, REG_CORE_CTRL, 0x80, 0x00, 0x80, r->core_ctrl2_per_chip_b3);
        write_reg_chip(addr, REG_CORE_CTRL, 0x80, 0x00, 0x82, 0xAA);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Step 12: misc registers
    write_reg(REG_MISC_SET, 0x00, 0x00, 0x44, 0x80);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(REG_ANALOG_MUX, 0x00, 0x00, 0x00, r->analog_mux_b3);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(REG_MISC_SET, 0x00, 0x00, 0x44, 0x80);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(REG_CORE_CTRL, 0x80, 0x00, 0x8D, 0xEE);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Steps 13-14: baud switch and freq ramp (order is chip-dependent)
    if (r->freq_before_baud) {
        do_freq_ramp(r, target_freq_mhz);
        do_baud_switch(r);
    } else {
        do_baud_switch(r);
        do_freq_ramp(r, target_freq_mhz);
    }

    // Step 15: hash counting register
    write_reg(REG_HASH_COUNT, 0x00, 0x00, 0x1E, 0xB5);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Step 16: ticket mask (written last to survive baud/freq transitions)
    set_ticket_mask(ASIC_TICKET_DIFF);
    vTaskDelay(pdMS_TO_TICKS(5));

    bb_log_i(r->tag, "%s init complete", r->tag);
    return ESP_OK;
}

bb_err_t bm137x_chip_quiesce(const bm137x_regs_t *r)
{
    bb_log_i(r->tag, "quiescing %s (CMD_INACTIVE)", r->tag);
    uint8_t inactive_data[2] = {0x00, 0x00};
    send_cmd(ASIC_CMD_INACTIVE, ASIC_GROUP_ALL, inactive_data, 2);
    vTaskDelay(pdMS_TO_TICKS(5));
    return BB_OK;
}

#endif // ASIC_CHIP
