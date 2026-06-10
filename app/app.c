#include "app.h"

#include "can.h"
#include "diagnostics.h"
#include "lsm6dsv16b.h"
#include "stm32g431_min.h"
#include "system_time.h"
#include "tdm_audio.h"
#include "uart_log.h"

#define LD2_PIN 8U
#define LED_TOGGLE_INTERVAL_MS 500U
#define UART_LOG_INTERVAL_MS 1000U

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

static void log_boot(void)
{
    uart_log_write("\nFingerFirmware ");
    uart_log_write(FIRMWARE_VERSION);
    uart_log_write("\n");
    uart_log_write("UART: USART2 115200 8N1\n");
}

static void log_i2c_sensor_status(void)
{
    uart_log_write(" i2c=");
    uart_log_write_u32((uint32_t)lsm6dsv16b_status());
    uart_log_write(" whoami=");
    uart_log_write_hex_u8(lsm6dsv16b_who_am_i());
}

static void log_accel_sample(void)
{
    lsm6dsv16b_accel_sample_t accel;

    if (!lsm6dsv16b_acceleration_mg(&accel)) {
        uart_log_write(" accel=invalid");
        return;
    }

    uart_log_write(" accel_mg=(");
    uart_log_write_i32(accel.x_mg);
    uart_log_write(",");
    uart_log_write_i32(accel.y_mg);
    uart_log_write(",");
    uart_log_write_i32(accel.z_mg);
    uart_log_write(")");
}

static void log_heartbeat(void)
{
    uart_log_write("t=");
    uart_log_write_u32(system_time_millis());
    uart_log_write("ms loops=");
    uart_log_write_u32(diagnostics_loop_count());
    log_i2c_sensor_status();
    log_accel_sample();
    uart_log_write("\n");
}

void app_init(void)
{
    system_time_init();
    uart_log_init();
    log_boot();

    led_init();
    diagnostics_init();
    can_init();
    lsm6dsv16b_init();
    tdm_audio_init();

    uart_log_write("init complete");
    log_i2c_sensor_status();
    uart_log_write("\n");
}

void app_update(void)
{
    static uint32_t last_led_toggle_ms;
    static uint32_t last_uart_log_ms;

    diagnostics_update();
    can_update();
    lsm6dsv16b_update();
    tdm_audio_update();

    if (system_time_elapsed(&last_led_toggle_ms, LED_TOGGLE_INTERVAL_MS)) {
        led_toggle();
    }

    if (system_time_elapsed(&last_uart_log_ms, UART_LOG_INTERVAL_MS)) {
        log_heartbeat();
    }
}
