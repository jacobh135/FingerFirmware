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

/* Write raw bytes verbatim (no newline translation). For binary streaming
   (e.g. live audio). Blocks until each byte is queued to the UART. */
void uart_log_write_bytes(const uint8_t *data, uint32_t len);

/* Change the USART2 baud rate at runtime. Waits for the current transmission
   to finish, reconfigures, and re-enables. baud must divide cleanly from the
   16 MHz UART clock for low error (e.g. 1000000 and 2000000 are exact;
   921600 is NOT and will garble). Returns the actual configured baud. */
uint32_t uart_log_set_baud(uint32_t baud);

#endif
