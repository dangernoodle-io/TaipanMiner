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

// I2C bus (shared: TPS546, EMC2101, SSD1306)
#define PIN_I2C_SDA        47
#define PIN_I2C_SCL        48
#define I2C_BUS_SPEED_HZ   400000
#define I2C_BUS_NUM         0

// TPS546029 voltage regulator (PMBus over I2C)
#define TPS546_I2C_ADDR    0x24

// EMC2101 fan/temperature controller
#define EMC2101_I2C_ADDR   0x4C

// SSD1306 OLED display (0.91")
#define SSD1306_I2C_ADDR   0x3C

// ADC power monitoring
#define PIN_ADC_VMON       2       // ADC1_CH1

// BOOT button
#define PIN_BOOT_BTN       0

// BM1368 operating parameters
#define BM1368_DEFAULT_MV       1166   // TPS546 VOUT target (mV)
#define BM1368_DEFAULT_FREQ_MHZ 490    // initial PLL target frequency (MHz)
#define BM1368_CHIP_COUNT       1      // number of BM1368 chips in chain
#define BM1368_JOB_INTERVAL_MS  500    // job dispatch interval (ms) — keeps ASIC fed

// Board overhead beyond the ASIC core rail (MCU, regulators, LEDs, OLED).
// Matches AxeOS FAMILY_SUPRA.power_offset so /api/power reports whole-board draw.
#define BOARD_POWER_OFFSET_MW   5000

// Nominal input voltage (5V rail)
#define BOARD_NOMINAL_VIN_MV    5000

// ASIC small cores and chip count for frequency tracking
#define BOARD_SMALL_CORES       1276
#define BOARD_ASIC_COUNT        1
