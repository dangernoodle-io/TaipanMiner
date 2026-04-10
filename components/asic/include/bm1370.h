#pragma once

// BM1370 chip identity
#define BM1370_CHIP_ID        0x1370
#define BM1370_CORES          80
#define BM1370_SMALL_CORES    16

// PLL fb_div range
#define BM1370_FB_MIN         160
#define BM1370_FB_MAX         239

// Register addresses
#define BM1370_REG_CHIP_ID    0x00
#define BM1370_REG_PLL        0x08
#define BM1370_REG_HASH_COUNT 0x10
#define BM1370_REG_MISC_CTRL  0x18
#define BM1370_REG_FAST_UART  0x28
#define BM1370_REG_CORE_CTRL  0x3C
#define BM1370_REG_ANALOG_MUX 0x54
#define BM1370_REG_IO_DRV     0x58
#define BM1370_REG_VERSION    0xA4
#define BM1370_REG_A8         0xA8
#define BM1370_REG_MISC_SET   0xB9
#define BM1370_REG_TICKET_MASK  0x14
