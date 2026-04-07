#pragma once

#include <stdint.h>

#define FONT_W 8
#define FONT_H 16

// ASCII 0x20 (' ') through 0x7E ('~') — 95 printable characters.
// Each character is 16 bytes, one byte per row, MSB is leftmost pixel.
extern const uint8_t g_font8x16[95][FONT_H];
