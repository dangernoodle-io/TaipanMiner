#include "asic_chip.h"
#ifdef ASIC_CHIP

#include "emc2101.h"
#include "emc2101_curve.h"
#include "esp_log.h"
#include "bb_log.h"
#include "esp_check.h"
#include "board.h"

static const char *TAG = "emc2101";

// EMC2101 registers
#define REG_INTERNAL_TEMP    0x00
#define REG_EXTERNAL_MSB     0x01
#define REG_CONFIG           0x03
#define REG_EXTERNAL_LSB     0x10
#define REG_IDEALITY_FACTOR  0x17
#define REG_BETA_COMPENSATION 0x18
#define REG_TACH_LSB         0x46
#define REG_TACH_MSB         0x47
#define REG_FAN_CONFIG       0x4A
#define REG_FAN_SETTING      0x4C

static i2c_master_dev_handle_t s_dev;
static int s_duty_pct = -1;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

bb_err_t emc2101_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add device");

    // Enable external diode, disable auto-fan
    ESP_RETURN_ON_ERROR(reg_write(REG_CONFIG, 0x04), TAG, "config");

    // Fan: direct PWM mode, enable driver, ~22.5kHz, 2 tach pulses/rev
    ESP_RETURN_ON_ERROR(reg_write(REG_FAN_CONFIG, 0x23), TAG, "fan config");

    // BM1370 thermal diode ideality + beta compensation (matches AxeOS for Gamma family).
    // Without these, the EMC2101 reports systematically wrong die temp.
#ifdef EMC2101_IDEALITY_FACTOR
    ESP_RETURN_ON_ERROR(reg_write(REG_IDEALITY_FACTOR, EMC2101_IDEALITY_FACTOR), TAG, "ideality");
#endif
#ifdef EMC2101_BETA_COMPENSATION
    ESP_RETURN_ON_ERROR(reg_write(REG_BETA_COMPENSATION, EMC2101_BETA_COMPENSATION), TAG, "beta");
#endif

    // Fail-safe: start at 100% until telemetry loop adjusts
    emc2101_set_duty_pct(100);

    bb_log_i(TAG, "initialized");
    return BB_OK;
}

bb_err_t emc2101_read_temp(float *temp_c)
{
    uint8_t msb, lsb;
    ESP_RETURN_ON_ERROR(reg_read(REG_EXTERNAL_MSB, &msb), TAG, "read MSB");
    ESP_RETURN_ON_ERROR(reg_read(REG_EXTERNAL_LSB, &lsb), TAG, "read LSB");

    uint16_t raw = ((uint16_t)msb << 8) | lsb;
    raw >>= 5;  // 11-bit signed value

    // Sign-extend from bit 10
    int16_t signed_val = (int16_t)(raw);
    if (raw & 0x400) {
        signed_val = (int16_t)(raw | 0xF800);
    }

    *temp_c = (float)signed_val / 8.0f;  // 0.125°C resolution
    return BB_OK;
}

bb_err_t emc2101_read_internal_temp(float *temp_c)
{
    uint8_t val;
    ESP_RETURN_ON_ERROR(reg_read(REG_INTERNAL_TEMP, &val), TAG, "read internal");
    *temp_c = (float)(int8_t)val;  // signed 8-bit, 1 degC resolution
    return BB_OK;
}

bb_err_t emc2101_set_fan_duty(uint8_t duty_0_63)
{
    if (duty_0_63 > 63) duty_0_63 = 63;
    return reg_write(REG_FAN_SETTING, duty_0_63);
}

void emc2101_set_duty_pct(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    // Map 0–100% → 0–63 raw (round to nearest)
    uint8_t raw = (uint8_t)((pct * 63 + 50) / 100);
    emc2101_set_fan_duty(raw);
    s_duty_pct = pct;
}

int emc2101_get_duty_pct(void)
{
    return s_duty_pct;
}

int emc2101_read_rpm(void)
{
    uint8_t lsb, msb;
    if (reg_read(REG_TACH_LSB, &lsb) != ESP_OK) return -1;
    if (reg_read(REG_TACH_MSB, &msb) != ESP_OK) return -1;
    uint16_t tach = (uint16_t)lsb | ((uint16_t)msb << 8);
    if (tach == 0xFFFF) return -1;
    return 5400000 / tach;
}

#endif // ASIC_BM1370 || ASIC_BM1368
