#include "i2c_sensors.h"

#include <stddef.h>

#include "stm32g431_min.h"
#include "system_time.h"

#define I2C_SCL_PIN 15U
#define I2C_SDA_PIN 7U
#define I2C_AF4 4U

#define I2C_TIMEOUT_MS 10U
#define IMU_SAMPLE_INTERVAL_MS 20U

#define LSM6DSV16B_ADDR 0x6AU
#define LSM6DSV16B_WHO_AM_I_VALUE 0x71U

#define LSM6DSV16B_REG_WHO_AM_I 0x0FU
#define LSM6DSV16B_REG_CTRL1 0x10U
#define LSM6DSV16B_REG_CTRL8 0x17U
#define LSM6DSV16B_REG_OUTZ_L_A 0x28U

#define LSM6DSV16B_CTRL1_XL_ODR_60HZ 0x05U
#define LSM6DSV16B_CTRL8_XL_FS_2G 0x00U

#define I2C_ERROR_FLAGS (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR | I2C_ISR_TIMEOUT)
#define I2C_CLEAR_FLAGS (I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF | I2C_ICR_TIMOUTCF)

static i2c_sensors_status_t sensor_status;
static i2c_sensors_accel_sample_t latest_accel;
static uint8_t imu_who_am_i;

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

static void i2c1_init(void)
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

static uint32_t i2c_cr2(uint8_t addr, size_t length, uint32_t flags)
{
    return (((uint32_t)addr << 1U) |
            ((uint32_t)length << I2C_CR2_NBYTES_Pos) |
            flags);
}

static bool i2c_write_register(uint8_t addr, uint8_t reg, const uint8_t *data, size_t length)
{
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

static bool i2c_read_registers(uint8_t addr, uint8_t reg, uint8_t *data, size_t length)
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

static bool lsm6dsv16b_write_u8(uint8_t reg, uint8_t value)
{
    return i2c_write_register(LSM6DSV16B_ADDR, reg, &value, 1U);
}

static bool lsm6dsv16b_read_u8(uint8_t reg, uint8_t *value)
{
    return i2c_read_registers(LSM6DSV16B_ADDR, reg, value, 1U);
}

static int16_t read_i16_le(const uint8_t *bytes)
{
    return (int16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static int32_t raw_accel_to_mg(int16_t raw)
{
    return ((int32_t)raw * 61) / 1000;
}

static bool lsm6dsv16b_read_accel(void)
{
    uint8_t bytes[6];

    if (!i2c_read_registers(LSM6DSV16B_ADDR, LSM6DSV16B_REG_OUTZ_L_A, bytes, sizeof(bytes))) {
        return false;
    }

    latest_accel.z_raw = read_i16_le(&bytes[0]);
    latest_accel.y_raw = read_i16_le(&bytes[2]);
    latest_accel.x_raw = read_i16_le(&bytes[4]);
    latest_accel.x_mg = raw_accel_to_mg(latest_accel.x_raw);
    latest_accel.y_mg = raw_accel_to_mg(latest_accel.y_raw);
    latest_accel.z_mg = raw_accel_to_mg(latest_accel.z_raw);
    latest_accel.timestamp_ms = system_time_millis();
    latest_accel.valid = true;

    return true;
}

void i2c_sensors_init(void)
{
    latest_accel.valid = false;
    imu_who_am_i = 0U;
    sensor_status = I2C_SENSORS_STATUS_NOT_PRESENT;

    i2c1_init();

    if (!lsm6dsv16b_read_u8(LSM6DSV16B_REG_WHO_AM_I, &imu_who_am_i)) {
        return;
    }

    if (imu_who_am_i != LSM6DSV16B_WHO_AM_I_VALUE) {
        return;
    }

    if (!lsm6dsv16b_write_u8(LSM6DSV16B_REG_CTRL8, LSM6DSV16B_CTRL8_XL_FS_2G) ||
        !lsm6dsv16b_write_u8(LSM6DSV16B_REG_CTRL1, LSM6DSV16B_CTRL1_XL_ODR_60HZ)) {
        sensor_status = I2C_SENSORS_STATUS_ERROR;
        return;
    }

    sensor_status = I2C_SENSORS_STATUS_READY;
}

void i2c_sensors_update(void)
{
    static uint32_t last_sample_ms;

    if (sensor_status != I2C_SENSORS_STATUS_READY) {
        return;
    }

    if (!system_time_elapsed(&last_sample_ms, IMU_SAMPLE_INTERVAL_MS)) {
        return;
    }

    if (!lsm6dsv16b_read_accel()) {
        latest_accel.valid = false;
        sensor_status = I2C_SENSORS_STATUS_ERROR;
    }
}

i2c_sensors_status_t i2c_sensors_status(void)
{
    return sensor_status;
}

uint8_t i2c_sensors_imu_who_am_i(void)
{
    return imu_who_am_i;
}

bool i2c_sensors_acceleration_mg(i2c_sensors_accel_sample_t *sample)
{
    if ((sample == NULL) || !latest_accel.valid) {
        return false;
    }

    *sample = latest_accel;
    return true;
}
