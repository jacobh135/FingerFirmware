#include "app.h"

#include "can.h"
#include "diagnostics.h"
#include "i2c_sensors.h"
#include "stm32g431_min.h"
#include "system_time.h"
#include "tdm_audio.h"

#define LD2_PIN 8U
#define LED_TOGGLE_INTERVAL_MS 500U

static void led_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    (void)RCC->AHB2ENR;

    GPIOB->MODER &= ~(3UL << (LD2_PIN * 2U));
    GPIOB->MODER |= (1UL << (LD2_PIN * 2U));
}

static void led_toggle(void)
{
    GPIOB->ODR ^= (1UL << LD2_PIN);
}

void app_init(void)
{
    system_time_init();
    led_init();
    diagnostics_init();
    can_init();
    i2c_sensors_init();
    tdm_audio_init();
}

void app_update(void)
{
    static uint32_t last_led_toggle_ms;

    diagnostics_update();
    can_update();
    i2c_sensors_update();
    tdm_audio_update();

    if (system_time_elapsed(&last_led_toggle_ms, LED_TOGGLE_INTERVAL_MS)) {
        led_toggle();
    }
}
