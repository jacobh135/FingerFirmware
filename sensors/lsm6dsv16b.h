#ifndef LSM6DSV16B_H
#define LSM6DSV16B_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LSM6DSV16B_STATUS_NOT_PRESENT = 0,
    LSM6DSV16B_STATUS_READY,
    LSM6DSV16B_STATUS_ERROR,
} lsm6dsv16b_status_t;

typedef struct {
    int16_t x_raw;
    int16_t y_raw;
    int16_t z_raw;
    int32_t x_mg;
    int32_t y_mg;
    int32_t z_mg;
    uint32_t timestamp_ms;
    bool valid;
} lsm6dsv16b_accel_sample_t;

void lsm6dsv16b_init(void);
void lsm6dsv16b_update(void);
lsm6dsv16b_status_t lsm6dsv16b_status(void);
uint8_t lsm6dsv16b_who_am_i(void);
bool lsm6dsv16b_acceleration_mg(lsm6dsv16b_accel_sample_t *sample);

#endif
