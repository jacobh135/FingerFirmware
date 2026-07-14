#include "app.h"

#include <stddef.h>

#include "can.h"
#include "diagnostics.h"
#include "lsm6dsv16b.h"
#include "stm32g431_min.h"
#include "system_time.h"
#include "tdm.h"
#include "texture.h"
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

static void log_tdm_status(void)
{
    int16_t x;
    int16_t y;
    int16_t z;

    uart_log_write(" tdm=");
    uart_log_write_u32((uint32_t)tdm_status());
    uart_log_write(" frames=");
    uart_log_write_u32(tdm_frame_count());

    if (tdm_latest_xyz(&x, &y, &z)) {
        uart_log_write(" audio=(");
        uart_log_write_i32((int32_t)x);
        uart_log_write(",");
        uart_log_write_i32((int32_t)y);
        uart_log_write(",");
        uart_log_write_i32((int32_t)z);
        uart_log_write(")");
    }
}

static void log_heartbeat(void)
{
    uart_log_write("t=");
    uart_log_write_u32(system_time_millis());
    uart_log_write("ms loops=");
    uart_log_write_u32(diagnostics_loop_count());
    log_i2c_sensor_status();
    log_accel_sample();
    log_tdm_status();
    uart_log_write("\n");
}

/* ----------------------------- capture trigger ---------------------------- *
 * Send 'c' over the serial monitor to arm a ~0.3 s full-rate 3-axis capture
 * of the audio-accelerometer channel. The heartbeat pauses while capturing
 * (keeps tdm_update() running fast and keeps the log output clean), then once
 * full, drains every sample as a "x y z" line, then resumes the heartbeat.
 * Feed the drained lines (capture-to-a-file, e.g. via the serial monitor's
 * own logging or `script`/tee) straight into tdm_analyze.py.
 * -------------------------------------------------------------------------- */
typedef enum {
    APP_MODE_NORMAL = 0,
    APP_MODE_CAPTURING,
    APP_MODE_DRAINING,
} app_mode_t;

static app_mode_t app_mode = APP_MODE_NORMAL;
static uint32_t drain_index;

/* Continuous texture-feature streaming. Toggled with 't'. When on, the 1 Hz
   heartbeat is suppressed and one feature line is emitted per completed window
   (~61/s) in a fixed, host-parseable format:
       feat <seq> <rms> <mad> <peak> <zcr> <crest> <mean>
   Classification happens off-board; this is just the raw feature stream. */
static bool texture_streaming;

/* Continuous LIVE AUDIO streaming. Toggled with 'a'. Raises the UART to 1 Mbaud
   and streams raw single-axis int16 samples continuously (binary, framed) so a
   host can reconstruct a live waveform / WAV. Single axis at full ~15.6 kHz
   keeps the whole audio bandwidth while fitting the link with margin. Mutually
   exclusive with texture streaming and the heartbeat. Uses the texture axis
   selection (x/y/z keys); default Z. */
static bool audio_streaming;
static tdm_stream_cursor_t audio_cursor;
static texture_axis_t audio_axis = TEXTURE_AXIS_Z;   /* default Z; x/y/z selects */
#define AUDIO_BAUD  1000000U
#define AUDIO_SYNC0 0xA5U
#define AUDIO_SYNC1 0x5AU
#define AUDIO_CHUNK 128U         /* samples per framed chunk (== ring depth) */

static void emit_feature_line(const texture_features_t *f)
{
    uart_log_write("feat ");
    uart_log_write_u32(f->seq);
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->rms);
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->mad);
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->peak);
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->zcr);
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->crest);
    uart_log_write(" ");
    uart_log_write_i32((int32_t)f->mean);
    /* Phase 2 spectral features */
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->band0);
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->band1);
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->band2);
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->band3);
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->centroid);
    uart_log_write(" ");
    uart_log_write_u32((uint32_t)f->peakfreq);
    uart_log_write("\n");
}

static void poll_capture_trigger(void)
{
    uint8_t byte;

    if (app_mode != APP_MODE_NORMAL) {
        return;
    }

    if (!uart_log_read_byte(&byte)) {
        return;
    }

    if (byte == (uint8_t)'c') {
        uart_log_write("\n[capture] armed, capturing ");
        uart_log_write_u32(tdm_capture_total_frames());
        uart_log_write(" frames...\n");
        tdm_capture_start();
        app_mode = APP_MODE_CAPTURING;
    } else if (byte == (uint8_t)'t') {
        /* Toggle continuous texture feature streaming. */
        texture_streaming = !texture_streaming;
        if (texture_streaming) {
            uart_log_write("\n[texture] streaming ON (feat seq rms mad peak zcr crest mean band0 band1 band2 band3 centroid peakfreq)\n");
        } else {
            uart_log_write("\n[texture] streaming OFF\n");
        }
    } else if (byte == (uint8_t)'a') {
        /* Toggle continuous LIVE AUDIO streaming (raw binary, 1 Mbaud). */
        audio_streaming = !audio_streaming;
        if (audio_streaming) {
            /* Announce (at the current baud) the axis + format, then switch up
               to the high baud so the host can re-open at 1 Mbaud. */
            uart_log_write("\n[audio] streaming ON axis=");
            uart_log_write((audio_axis == TEXTURE_AXIS_X) ? "x"
                         : (audio_axis == TEXTURE_AXIS_Y) ? "y" : "z");
            uart_log_write(" fmt=int16le sync=A55A baud=1000000\n");
            uart_log_flush();
            tdm_stream_cursor_init(&audio_cursor);
            (void)uart_log_set_baud(AUDIO_BAUD);
        } else {
            /* Drop back to the normal log baud and announce off. */
            (void)uart_log_set_baud(115200U);
            uart_log_write("\n[audio] streaming OFF\n");
        }
    } else if ((byte == (uint8_t)'x') || (byte == (uint8_t)'y') || (byte == (uint8_t)'z')) {
        /* Select which axis both texture features AND audio use. */
        texture_axis_t axis = (byte == (uint8_t)'x') ? TEXTURE_AXIS_X
                            : (byte == (uint8_t)'y') ? TEXTURE_AXIS_Y
                            : TEXTURE_AXIS_Z;
        texture_set_axis(axis);
        audio_axis = axis;
        uart_log_write("\n[texture] axis=");
        uart_log_write((byte == (uint8_t)'x') ? "x"
                     : (byte == (uint8_t)'y') ? "y" : "z");
        uart_log_write("\n");
    }
}

static void drain_one_line(void)
{
    int16_t x;
    int16_t y;
    int16_t z;

    if (!tdm_capture_get(drain_index, &x, &y, &z)) {
        /* Done draining. */
        uart_log_write("[capture] done\n");
        app_mode = APP_MODE_NORMAL;
        return;
    }

    uart_log_write_i32((int32_t)x);
    uart_log_write(" ");
    uart_log_write_i32((int32_t)y);
    uart_log_write(" ");
    uart_log_write_i32((int32_t)z);
    uart_log_write("\n");

    drain_index++;
}

void app_init(void)
{
    system_time_init();
    uart_log_init();
    uart_log_write("\n[boot] M1 uart up\n");
    uart_log_flush();
    log_boot();
    uart_log_write("[boot] M2 after log_boot\n");
    uart_log_flush();

    led_init();
    diagnostics_init();
    can_init();
    uart_log_write("[boot] M3 after can\n");
    uart_log_flush();
    lsm6dsv16b_init();
    uart_log_write("[boot] M4 after imu\n");
    uart_log_flush();
    tdm_init();
    uart_log_write("[boot] M5 after tdm\n");
    uart_log_flush();
    texture_init(TEXTURE_AXIS_Z);   /* default axis; change live with x/y/z */
    uart_log_write("[boot] M6 after texture/fft\n");
    uart_log_flush();

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
    tdm_update();
    if (!audio_streaming) {
        /* Skip the per-window FFT while live-audio streaming -- those cycles
           are needed to drain the audio stream, and features aren't used in
           audio mode anyway. */
        texture_update();
    }

    if (system_time_elapsed(&last_led_toggle_ms, LED_TOGGLE_INTERVAL_MS)) {
        led_toggle();
    }

    switch (app_mode) {
        case APP_MODE_NORMAL:
            poll_capture_trigger();

            if (audio_streaming) {
                /* LIVE AUDIO: pull all newly-arrived frames' selected axis and
                   push them out as framed binary as fast as the (1 Mbaud) UART
                   allows. The link has ~3x margin over the single-axis data
                   rate, so we drain faster than samples arrive and the live
                   ring never overflows. Framing: [0xA5 0x5A][count_lo count_hi]
                   [int16 le samples...] so the host can lock on and spot gaps. */
                int16_t axbuf[AUDIO_CHUNK];
                int16_t *xp = (audio_axis == TEXTURE_AXIS_X) ? axbuf : NULL;
                int16_t *yp = (audio_axis == TEXTURE_AXIS_Y) ? axbuf : NULL;
                int16_t *zp = (audio_axis == TEXTURE_AXIS_Z) ? axbuf : NULL;

                uint32_t got = tdm_stream_pull(&audio_cursor, xp, yp, zp,
                                               AUDIO_CHUNK);
                if (got > 0U) {
                    uint8_t hdr[4];
                    hdr[0] = AUDIO_SYNC0;
                    hdr[1] = AUDIO_SYNC1;
                    hdr[2] = (uint8_t)(got & 0xFFU);
                    hdr[3] = (uint8_t)((got >> 8) & 0xFFU);
                    uart_log_write_bytes(hdr, 4U);
                    uart_log_write_bytes((const uint8_t *)axbuf, got * 2U);
                }
            } else if (texture_streaming) {
                /* Emit every completed feature window. While streaming we skip
                   the 1 Hz heartbeat so the host sees a clean feature-only
                   stream. texture_take() returns at most one window per call;
                   the main loop runs far faster than the ~61 windows/s rate,
                   so none are missed. */
                texture_features_t f;
                if (texture_take(&f)) {
                    emit_feature_line(&f);
                }
            } else if (system_time_elapsed(&last_uart_log_ms, UART_LOG_INTERVAL_MS)) {
                log_heartbeat();
            }
            break;

        case APP_MODE_CAPTURING:
            /* tdm_update() above is filling the capture buffer. Nothing else
               to do here but wait -- avoid the heartbeat's UART writes during
               capture so tdm_update() gets called as fast as possible (see
               the timing note in tdm.c about the circular buffer's ~8.2 ms
               wrap window). */
            if (tdm_capture_ready()) {
                uart_log_write("[capture] captured ");
                uart_log_write_u32(tdm_capture_count());
                uart_log_write(" frames, draining (x y z per line)...\n");
                drain_index = 0U;
                app_mode = APP_MODE_DRAINING;
            }
            break;

        case APP_MODE_DRAINING:
            /* Drain one sample per main-loop iteration. uart_log_putc_raw()
               blocks on TXE, so this naturally paces itself to whatever the
               UART can sustain at 115200 baud -- no separate rate limiting
               needed. */
            drain_one_line();
            break;

        default:
            app_mode = APP_MODE_NORMAL;
            break;
    }
}
