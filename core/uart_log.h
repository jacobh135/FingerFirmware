#ifndef UART_LOG_H
#define UART_LOG_H

#include <stdbool.h>
#include <stdint.h>

void uart_log_init(void);
void uart_log_flush(void);
void uart_log_write(const char *text);
void uart_log_write_u32(uint32_t value);
void uart_log_write_i32(int32_t value);
void uart_log_write_hex_u8(uint8_t value);

/* Non-blocking read of one received byte. Returns true and fills *out_byte if
   a byte is available; returns false immediately otherwise (does not block).
   USART2 RX is already enabled (CR1 RE) and wired on PA3; this just reads it. */
bool uart_log_read_byte(uint8_t *out_byte);

#endif
