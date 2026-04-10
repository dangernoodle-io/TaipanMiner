#pragma once

#define BOARD_NAME "bitaxe-403"

// BM1368 ASIC (UART1)
#define PIN_ASIC_TX        17
#define PIN_ASIC_RX        18
#define ASIC_UART_NUM      1       // UART_NUM_1
#define ASIC_BAUD_INIT     115200
#define ASIC_BAUD_FAST     1000000

// ASIC control
#define PIN_ASIC_RST       1       // active-low reset
#define PIN_ASIC_EN        10      // power enable

// I2C bus (shared: DS4432, EMC2101, SSD1306)
#define PIN_I2C_SDA        47
#define PIN_I2C_SCL        48
#define I2C_BUS_SPEED_HZ   400000
#define I2C_BUS_NUM         0

// DS4432U+ voltage DAC (controls TPS40305 buck converter)
#define DS4432_I2C_ADDR    0x48

// EMC2101 fan/temperature controller
#define EMC2101_I2C_ADDR   0x4C

// SSD1306 OLED display (0.91")
#define SSD1306_I2C_ADDR   0x3C

// ADC power monitoring
#define PIN_ADC_VMON       2       // ADC1_CH1

// BOOT button
#define PIN_BOOT_BTN       0

// BM1368 operating parameters
#define BM1368_DEFAULT_MV       1166   // DS4432 voltage target (mV)
#define BM1368_DEFAULT_FREQ_MHZ 490    // initial PLL target frequency (MHz)
#define BM1368_CHIP_COUNT       1      // number of BM1368 chips in chain
#define BM1368_JOB_INTERVAL_MS  500    // job dispatch interval (ms) — keeps ASIC fed
