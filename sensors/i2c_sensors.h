#ifndef I2C_SENSORS_H
#define I2C_SENSORS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    I2C_SENSORS_STATUS_NOT_PRESENT = 0,
    I2C_SENSORS_STATUS_READY,
    I2C_SENSORS_STATUS_ERROR,
} i2c_sensors_status_t;

typedef struct {
    int16_t x_raw;
    int16_t y_raw;
    int16_t z_raw;
    int32_t x_mg;
    int32_t y_mg;
    int32_t z_mg;
    uint32_t timestamp_ms;
    bool valid;
} i2c_sensors_accel_sample_t;

void i2c_sensors_init(void);
void i2c_sensors_update(void);
i2c_sensors_status_t i2c_sensors_status(void);
uint8_t i2c_sensors_imu_who_am_i(void);
bool i2c_sensors_acceleration_mg(i2c_sensors_accel_sample_t *sample);

#endif
