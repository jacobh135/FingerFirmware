#include "uart_log.h"

#include <stdbool.h>
#include <stddef.h>

#include "stm32g431_min.h"

#define UART_LOG_TX_PIN 2U
#define UART_LOG_RX_PIN 3U
#define UART_LOG_AF_USART2 7U
#define UART_LOG_BAUD 115200U
#define UART_LOG_PCLK_HZ 16000000U
#define UART_LOG_BRR ((UART_LOG_PCLK_HZ + (UART_LOG_BAUD / 2U)) / UART_LOG_BAUD)

/* USART receive bits not present in stm32g431_min.h (it defines only the TX
   bits used so far). Bit positions per the STM32G4 reference manual / CMSIS:
   RXNE = ISR bit 5, ORE = ISR bit 3, ORECF = ICR bit 3. */
#define USART_ISR_RXNE_RXFNE (1UL << 5)
#define USART_ISR_ORE        (1UL << 3)
#define USART_ICR_ORECF      (1UL << 3)

static int uart_log_ready;

static void gpio_set_alternate_function(GPIO_TypeDef *gpio, uint32_t pin, uint32_t af)
{
    if (pin < 8U) {
        gpio->AFRL &= ~(0xFUL << (pin * 4U));
        gpio->AFRL |= (af << (pin * 4U));
    } else {
        uint32_t shift = (pin - 8U) * 4U;

        gpio->AFRH &= ~(0xFUL << shift);
        gpio->AFRH |= (af << shift);
    }
}

static void uart_log_gpio_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    (void)RCC->AHB2ENR;

    GPIOA->MODER &= ~(3UL << (UART_LOG_TX_PIN * 2U));
    GPIOA->MODER |= (2UL << (UART_LOG_TX_PIN * 2U));
    GPIOA->OTYPER &= ~(1UL << UART_LOG_TX_PIN);
    GPIOA->OSPEEDR |= (3UL << (UART_LOG_TX_PIN * 2U));
    GPIOA->PUPDR &= ~(3UL << (UART_LOG_TX_PIN * 2U));
    gpio_set_alternate_function(GPIOA, UART_LOG_TX_PIN, UART_LOG_AF_USART2);

    GPIOA->MODER &= ~(3UL << (UART_LOG_RX_PIN * 2U));
    GPIOA->MODER |= (2UL << (UART_LOG_RX_PIN * 2U));
    GPIOA->OTYPER &= ~(1UL << UART_LOG_RX_PIN);
    GPIOA->OSPEEDR |= (3UL << (UART_LOG_RX_PIN * 2U));
    GPIOA->PUPDR &= ~(3UL << (UART_LOG_RX_PIN * 2U));
    GPIOA->PUPDR |= (1UL << (UART_LOG_RX_PIN * 2U));
    gpio_set_alternate_function(GPIOA, UART_LOG_RX_PIN, UART_LOG_AF_USART2);
}

static void uart_log_putc_raw(char ch)
{
    while ((USART2->ISR & USART_ISR_TXE_TXFNF) == 0U) {
    }

    USART2->TDR = (uint32_t)(uint8_t)ch;
}

static void uart_log_putc(char ch)
{
    if (ch == '\n') {
        uart_log_putc_raw('\r');
    }

    uart_log_putc_raw(ch);
}

void uart_log_init(void)
{
    uart_log_gpio_init();

    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    (void)RCC->APB1ENR1;

    USART2->CR1 = 0U;
    USART2->CR2 = 0U;
    USART2->CR3 = 0U;
    USART2->BRR = UART_LOG_BRR;
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    uart_log_ready = 1;
}

void uart_log_flush(void)
{
    if (!uart_log_ready) {
        return;
    }

    while ((USART2->ISR & USART_ISR_TC) == 0U) {
    }
}

void uart_log_write(const char *text)
{
    if (!uart_log_ready || (text == NULL)) {
        return;
    }

    while (*text != '\0') {
        uart_log_putc(*text++);
    }
}

void uart_log_write_u32(uint32_t value)
{
    char digits[10];
    size_t count = 0U;

    if (value == 0U) {
        uart_log_putc('0');
        return;
    }

    while (value != 0U) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (count > 0U) {
        uart_log_putc(digits[--count]);
    }
}

void uart_log_write_i32(int32_t value)
{
    if (value < 0) {
        uart_log_putc('-');
        uart_log_write_u32((uint32_t)(-(value + 1)) + 1U);
        return;
    }

    uart_log_write_u32((uint32_t)value);
}

void uart_log_write_hex_u8(uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    uart_log_write("0x");
    uart_log_putc(hex[(value >> 4) & 0x0FU]);
    uart_log_putc(hex[value & 0x0FU]);
}

int _write(int file, char *ptr, int len)
{
    (void)file;

    if (!uart_log_ready || (ptr == NULL) || (len < 0)) {
        return 0;
    }

    for (int i = 0; i < len; i++) {
        uart_log_putc(ptr[i]);
    }

    return len;
}

bool uart_log_read_byte(uint8_t *out_byte)
{
    if (!uart_log_ready || (out_byte == NULL)) {
        return false;
    }

    /* Clear a pending overrun (e.g. bytes arrived while we weren't polling)
       so RXNE keeps reporting new data instead of getting stuck. Reading RDR
       does not clear ORE; it must be cleared explicitly via ICR. */
    if ((USART2->ISR & USART_ISR_ORE) != 0U) {
        USART2->ICR = USART_ICR_ORECF;
    }

    if ((USART2->ISR & USART_ISR_RXNE_RXFNE) == 0U) {
        return false;
    }

    *out_byte = (uint8_t)USART2->RDR;
    return true;
}

void uart_log_write_bytes(const uint8_t *data, uint32_t len)
{
    if (!uart_log_ready || (data == NULL)) {
        return;
    }

    for (uint32_t i = 0U; i < len; i++) {
        while ((USART2->ISR & USART_ISR_TXE_TXFNF) == 0U) {
        }
        USART2->TDR = (uint32_t)data[i];
    }
}

uint32_t uart_log_set_baud(uint32_t baud)
{
    if (!uart_log_ready || (baud == 0U)) {
        return 0U;
    }

    /* Let any in-flight byte finish before we change the divisor. */
    while ((USART2->ISR & USART_ISR_TC) == 0U) {
    }

    /* Disable, reprogram BRR, re-enable. Rounded divide for lowest error. */
    USART2->CR1 &= ~USART_CR1_UE;
    uint32_t brr = (UART_LOG_PCLK_HZ + (baud / 2U)) / baud;
    USART2->BRR = brr;
    USART2->CR1 |= USART_CR1_UE;

    return UART_LOG_PCLK_HZ / brr;   /* actual baud achieved */
}
