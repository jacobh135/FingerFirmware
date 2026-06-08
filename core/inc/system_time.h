#ifndef SYSTEM_TIME_H
#define SYSTEM_TIME_H

#include <stdbool.h>
#include <stdint.h>

void system_time_init(void);
uint32_t system_time_millis(void);
bool system_time_elapsed(uint32_t *last_ms, uint32_t interval_ms);

#endif
