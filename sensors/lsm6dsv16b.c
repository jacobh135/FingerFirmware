#include "lsm6dsv16b.h"

#include <stddef.h>

#include "i2c.h"
#include "system_time.h"

#define IMU_SAMPLE_INTERVAL_MS 20U

#define LSM6DSV16B_ADDR 0x6AU
#define LSM6DSV16B_WHO_AM_I_VALUE 0x71U

#define LSM6DSV16B_REG_WHO_AM_I 0x0FU
#define LSM6DSV16B_REG_CTRL1 0x10U
#define LSM6DSV16B_REG_CTRL8 0x17U
#define LSM6DSV16B_REG_OUTZ_L_A 0x28U

#define LSM6DSV16B_CTRL1_XL_ODR_60HZ 0x05U
#define LSM6DSV16B_CTRL8_XL_FS_2G 0x00U

static lsm6dsv16b_status_t sensor_status;
static lsm6dsv16b_accel_sample_t latest_accel;
static uint8_t imu_who_am_i;

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

void lsm6dsv16b_init(void)
{
    latest_accel.valid = false;
    imu_who_am_i = 0U;
    sensor_status = LSM6DSV16B_STATUS_NOT_PRESENT;

    i2c_init();

    if (!lsm6dsv16b_read_u8(LSM6DSV16B_REG_WHO_AM_I, &imu_who_am_i)) {
        return;
    }

    if (imu_who_am_i != LSM6DSV16B_WHO_AM_I_VALUE) {
        return;
    }

    if (!lsm6dsv16b_write_u8(LSM6DSV16B_REG_CTRL8, LSM6DSV16B_CTRL8_XL_FS_2G) ||
        !lsm6dsv16b_write_u8(LSM6DSV16B_REG_CTRL1, LSM6DSV16B_CTRL1_XL_ODR_60HZ)) {
        sensor_status = LSM6DSV16B_STATUS_ERROR;
        return;
    }

    sensor_status = LSM6DSV16B_STATUS_READY;
}

void lsm6dsv16b_update(void)
{
    static uint32_t last_sample_ms;

    if (sensor_status != LSM6DSV16B_STATUS_READY) {
        return;
    }

    if (!system_time_elapsed(&last_sample_ms, IMU_SAMPLE_INTERVAL_MS)) {
        return;
    }

    if (!lsm6dsv16b_read_accel()) {
        latest_accel.valid = false;
        sensor_status = LSM6DSV16B_STATUS_ERROR;
    }
}

lsm6dsv16b_status_t lsm6dsv16b_status(void)
{
    return sensor_status;
}

uint8_t lsm6dsv16b_who_am_i(void)
{
    return imu_who_am_i;
}

bool lsm6dsv16b_acceleration_mg(lsm6dsv16b_accel_sample_t *sample)
{
    if ((sample == NULL) || !latest_accel.valid) {
        return false;
    }

    *sample = latest_accel;
    return true;
}
