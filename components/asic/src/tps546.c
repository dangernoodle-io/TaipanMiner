#ifdef ASIC_BM1370

#include "tps546.h"
#include "esp_log.h"
#include "esp_check.h"
#include <string.h>

static const char *TAG = "tps546";

// PMBus registers
#define PMBUS_OPERATION    0x01
#define PMBUS_VOUT_MODE    0x20
#define PMBUS_VOUT_COMMAND 0x21

// OPERATION values
#define OPERATION_ON  0x80
#define OPERATION_OFF 0x00

static i2c_master_dev_handle_t s_dev;
static int8_t s_vout_n;  // VOUT_MODE exponent (negative, e.g. -9)

// Read one byte from a PMBus register
static esp_err_t pmbus_read_byte(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

// Write one byte to a PMBus register
static esp_err_t pmbus_write_byte(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

// Write two bytes (word) to a PMBus register (little-endian)
static esp_err_t pmbus_write_word(uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = {reg, val & 0xFF, (val >> 8) & 0xFF};
    return i2c_master_transmit(s_dev, buf, 3, 100);
}

// Encode millivolts to ULINEAR16 using the VOUT_MODE exponent
static uint16_t mv_to_ulinear16(uint16_t mv)
{
    // ULINEAR16: value = voltage / 2^N where N is negative
    // So: code = voltage_V * 2^(-N) = (mv / 1000) * 2^(-N)
    // Using integer math: code = mv * 2^(-N) / 1000
    int shift = -s_vout_n;
    return (uint16_t)((uint32_t)mv * (1U << shift) / 1000);
}

esp_err_t tps546_init(i2c_master_bus_handle_t bus, uint8_t addr, uint16_t target_mv)
{
    // Add device to I2C bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add device");

    // Read VOUT_MODE to get exponent
    uint8_t vout_mode;
    ESP_RETURN_ON_ERROR(pmbus_read_byte(PMBUS_VOUT_MODE, &vout_mode), TAG, "read VOUT_MODE");

    // Extract 5-bit signed exponent from bits[4:0]
    s_vout_n = (int8_t)(vout_mode & 0x1F);
    if (s_vout_n & 0x10) {
        s_vout_n |= 0xE0;  // sign-extend from bit 4
    }
    ESP_LOGI(TAG, "VOUT_MODE=0x%02X exponent=%d", vout_mode, s_vout_n);

    // Set output voltage
    uint16_t code = mv_to_ulinear16(target_mv);
    ESP_RETURN_ON_ERROR(pmbus_write_word(PMBUS_VOUT_COMMAND, code), TAG, "set VOUT");
    ESP_LOGI(TAG, "VOUT_COMMAND=0x%04X (%u mV)", code, target_mv);

    // Power on
    ESP_RETURN_ON_ERROR(pmbus_write_byte(PMBUS_OPERATION, OPERATION_ON), TAG, "power on");
    ESP_LOGI(TAG, "powered on at %u mV", target_mv);

    return ESP_OK;
}

esp_err_t tps546_set_voltage_mv(uint16_t target_mv)
{
    uint16_t code = mv_to_ulinear16(target_mv);
    ESP_RETURN_ON_ERROR(pmbus_write_word(PMBUS_VOUT_COMMAND, code), TAG, "set VOUT");
    ESP_LOGI(TAG, "VOUT_COMMAND=0x%04X (%u mV)", code, target_mv);
    return ESP_OK;
}

#endif // ASIC_BM1370
