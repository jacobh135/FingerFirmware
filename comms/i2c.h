#ifndef I2C_H
#define I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void i2c_init(void);
bool i2c_write_register(uint8_t addr, uint8_t reg, const uint8_t *data, size_t length);
bool i2c_read_registers(uint8_t addr, uint8_t reg, uint8_t *data, size_t length);

#endif
