#include "system_time.h"

#define SYSTEM_CORE_CLOCK_HZ 16000000U
#define SYSTEM_TICK_HZ 1000U

#define SYSTICK_BASE (0xE000E010UL)

typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
} SysTick_TypeDef;

#define SYSTICK ((SysTick_TypeDef *)SYSTICK_BASE)

#define SYSTICK_CTRL_ENABLE (1UL << 0)
#define SYSTICK_CTRL_TICKINT (1UL << 1)
#define SYSTICK_CTRL_CLKSOURCE (1UL << 2)

static volatile uint32_t system_ticks_ms;

void system_time_init(void)
{
    SYSTICK->LOAD = (SYSTEM_CORE_CLOCK_HZ / SYSTEM_TICK_HZ) - 1U;
    SYSTICK->VAL = 0U;
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;
}

uint32_t system_time_millis(void)
{
    return system_ticks_ms;
}

bool system_time_elapsed(uint32_t *last_ms, uint32_t interval_ms)
{
    uint32_t now_ms = system_time_millis();

    if ((uint32_t)(now_ms - *last_ms) < interval_ms) {
        return false;
    }

    *last_ms = now_ms;
    return true;
}

void SysTick_Handler(void)
{
    system_ticks_ms++;
}
