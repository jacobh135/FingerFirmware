#ifndef UART_LOG_H
#define UART_LOG_H

#include <stdint.h>

void uart_log_init(void);
void uart_log_flush(void);
void uart_log_write(const char *text);
void uart_log_write_u32(uint32_t value);
void uart_log_write_i32(int32_t value);
void uart_log_write_hex_u8(uint8_t value);

#endif
