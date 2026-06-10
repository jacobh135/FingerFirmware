#include "i2c.h"

#include "stm32g431_min.h"
#include "system_time.h"

#define I2C_SCL_PIN 15U
#define I2C_SDA_PIN 7U
#define I2C_AF4 4U

#define I2C_TIMEOUT_MS 10U

#define I2C_ERROR_FLAGS (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR | I2C_ISR_TIMEOUT)
#define I2C_CLEAR_FLAGS (I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF | I2C_ICR_TIMOUTCF)

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

static void i2c_gpio_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;
    (void)RCC->AHB2ENR;

    GPIOA->MODER &= ~(3UL << (I2C_SCL_PIN * 2U));
    GPIOA->MODER |= (2UL << (I2C_SCL_PIN * 2U));
    GPIOA->OTYPER |= (1UL << I2C_SCL_PIN);
    GPIOA->OSPEEDR |= (3UL << (I2C_SCL_PIN * 2U));
    GPIOA->PUPDR &= ~(3UL << (I2C_SCL_PIN * 2U));
    GPIOA->PUPDR |= (1UL << (I2C_SCL_PIN * 2U));
    gpio_set_alternate_function(GPIOA, I2C_SCL_PIN, I2C_AF4);

    GPIOB->MODER &= ~(3UL << (I2C_SDA_PIN * 2U));
    GPIOB->MODER |= (2UL << (I2C_SDA_PIN * 2U));
    GPIOB->OTYPER |= (1UL << I2C_SDA_PIN);
    GPIOB->OSPEEDR |= (3UL << (I2C_SDA_PIN * 2U));
    GPIOB->PUPDR &= ~(3UL << (I2C_SDA_PIN * 2U));
    GPIOB->PUPDR |= (1UL << (I2C_SDA_PIN * 2U));
    gpio_set_alternate_function(GPIOB, I2C_SDA_PIN, I2C_AF4);
}

static void i2c_clear_flags(void)
{
    I2C1->ICR = I2C_CLEAR_FLAGS;
}

static bool i2c_timed_out(uint32_t start_ms, uint32_t timeout_ms)
{
    return (uint32_t)(system_time_millis() - start_ms) >= timeout_ms;
}

static bool i2c_wait_until_clear(uint32_t flag, uint32_t timeout_ms)
{
    uint32_t start_ms = system_time_millis();

    while ((I2C1->ISR & flag) != 0U) {
        if (i2c_timed_out(start_ms, timeout_ms)) {
            return false;
        }
    }

    return true;
}

static bool i2c_wait_for_flag(uint32_t flag, uint32_t timeout_ms)
{
    uint32_t start_ms = system_time_millis();

    while ((I2C1->ISR & flag) == 0U) {
        if ((I2C1->ISR & I2C_ERROR_FLAGS) != 0U) {
            return false;
        }

        if (i2c_timed_out(start_ms, timeout_ms)) {
            return false;
        }
    }

    return true;
}

static void i2c_abort_transfer(void)
{
    if ((I2C1->ISR & I2C_ISR_BUSY) != 0U) {
        I2C1->CR2 |= I2C_CR2_STOP;
    }

    i2c_clear_flags();
}

static uint32_t i2c_cr2(uint8_t addr, size_t length, uint32_t flags)
{
    return (((uint32_t)addr << 1U) |
            ((uint32_t)length << I2C_CR2_NBYTES_Pos) |
            flags);
}

void i2c_init(void)
{
    i2c_gpio_init();

    RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
    (void)RCC->APB1ENR1;

    RCC->APB1RSTR1 |= RCC_APB1RSTR1_I2C1RST;
    RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_I2C1RST;

    I2C1->CR1 = 0U;
    I2C1->TIMINGR = 0x30420F13U; /* 100 kHz from the reset-default 16 MHz HSI clock. */
    i2c_clear_flags();
    I2C1->CR1 = I2C_CR1_PE;
}

bool i2c_write_register(uint8_t addr, uint8_t reg, const uint8_t *data, size_t length)
{
    if ((data == NULL) && (length > 0U)) {
        return false;
    }

    if ((length + 1U) > 255U) {
        return false;
    }

    if (!i2c_wait_until_clear(I2C_ISR_BUSY, I2C_TIMEOUT_MS)) {
        return false;
    }

    i2c_clear_flags();
    I2C1->CR2 = i2c_cr2(addr, length + 1U, I2C_CR2_AUTOEND | I2C_CR2_START);

    if (!i2c_wait_for_flag(I2C_ISR_TXIS, I2C_TIMEOUT_MS)) {
        i2c_abort_transfer();
        return false;
    }
    I2C1->TXDR = reg;

    for (size_t i = 0; i < length; i++) {
        if (!i2c_wait_for_flag(I2C_ISR_TXIS, I2C_TIMEOUT_MS)) {
            i2c_abort_transfer();
            return false;
        }
        I2C1->TXDR = data[i];
    }

    if (!i2c_wait_for_flag(I2C_ISR_STOPF, I2C_TIMEOUT_MS)) {
        i2c_abort_transfer();
        return false;
    }

    i2c_clear_flags();
    return true;
}

bool i2c_read_registers(uint8_t addr, uint8_t reg, uint8_t *data, size_t length)
{
    if ((data == NULL) || (length == 0U) || (length > 255U)) {
        return false;
    }

    if (!i2c_wait_until_clear(I2C_ISR_BUSY, I2C_TIMEOUT_MS)) {
        return false;
    }

    i2c_clear_flags();
    I2C1->CR2 = i2c_cr2(addr, 1U, I2C_CR2_START);

    if (!i2c_wait_for_flag(I2C_ISR_TXIS, I2C_TIMEOUT_MS)) {
        i2c_abort_transfer();
        return false;
    }
    I2C1->TXDR = reg;

    if (!i2c_wait_for_flag(I2C_ISR_TC, I2C_TIMEOUT_MS)) {
        i2c_abort_transfer();
        return false;
    }

    I2C1->CR2 = i2c_cr2(addr, length, I2C_CR2_RD_WRN | I2C_CR2_AUTOEND | I2C_CR2_START);

    for (size_t i = 0; i < length; i++) {
        if (!i2c_wait_for_flag(I2C_ISR_RXNE, I2C_TIMEOUT_MS)) {
            i2c_abort_transfer();
            return false;
        }
        data[i] = (uint8_t)I2C1->RXDR;
    }

    if (!i2c_wait_for_flag(I2C_ISR_STOPF, I2C_TIMEOUT_MS)) {
        i2c_abort_transfer();
        return false;
    }

    i2c_clear_flags();
    return true;
}
