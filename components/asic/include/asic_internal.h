#pragma once
#ifdef ASIC_CHIP

#include <stdint.h>
#include <stddef.h>

// UART helpers (defined in asic_task.c)
int  asic_uart_read(uint8_t *buf, size_t len, uint32_t timeout_ms);
void asic_uart_write(const uint8_t *buf, size_t len);

// Command helpers (defined in asic_task.c)
void send_cmd(uint8_t cmd, uint8_t group, const uint8_t *data, uint8_t data_len);
void write_reg(uint8_t reg, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3);
void write_reg_chip(uint8_t chip_addr, uint8_t reg, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3);
void set_ticket_mask(double difficulty);

#endif
