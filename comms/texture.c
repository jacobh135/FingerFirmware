#include "texture.h"

#include <stddef.h>

#include "fft.h"
#include "tdm.h"

/* -------------------------------------------------------------------------- *
 * Time-domain feature extraction over fixed windows of one accelerometer axis.
 * No FFT: every feature here is a single pass (or two) over the window in the
 * time domain, cheap enough to run inline in the main loop on the G431's FPU.
 *
 * Pipeline per call:
 *   1. tdm_stream_pull() -> append new frames' chosen-axis samples to window[]
 *   2. when window[] fills (TEXTURE_WINDOW_SAMPLES), compute features:
 *        mean  = average (DC: gravity + bias on this axis)
 *        rms   = sqrt(mean((s-mean)^2))          intensity / roughness
 *        mad   = mean(|s-mean|)                  robust roughness
 *        peak  = max(|s-mean|)                   transients / catches
 *        zcr   = sign changes of (s-mean)        frequency proxy
 *        crest = 100 * peak / rms                impact vs steady hiss
 *   3. latch features, raise ready flag, reset window
 * -------------------------------------------------------------------------- */

static texture_axis_t cur_axis;
static tdm_stream_cursor_t cursor;

static int16_t window[TEXTURE_WINDOW_SAMPLES];
static uint32_t fill;             /* how many samples currently in window[]   */

static texture_features_t latched;
static bool ready;
static uint32_t seq_counter;

/* Scratch arrays for one pull. Small on purpose: texture_update() loops,
   pulling repeatedly until the stream is drained, so these don't need to hold
   a whole window -- keeping them small saves RAM that the burst-capture buffer
   and stack also need to share in the G431's 32 KB. */
#define PULL_MAX 64U
static int16_t pull_x[PULL_MAX];
static int16_t pull_y[PULL_MAX];
static int16_t pull_z[PULL_MAX];

static void reset_window(void)
{
    fill = 0U;
}

/* Integer square root (binary digit-by-digit). Used for RMS so the module has
   no floating-point or libm dependency -- inputs and outputs here are all
   integer counts, so an exact integer sqrt is the natural fit and keeps the
   feature math fully deterministic across toolchains/optimization levels. */
static uint32_t isqrt_u64(uint64_t value)
{
    uint64_t rem = 0U;
    uint64_t root = 0U;

    for (int i = 0; i < 32; i++) {
        root <<= 1;
        rem = (rem << 2) | (value >> 62);
        value <<= 2;
        if (rem > root) {
            rem -= (root | 1U);
            root += 2U;
        }
    }

    return (uint32_t)(root >> 1);
}

void texture_init(texture_axis_t axis)
{
    fft_init();               /* build FFT twiddle/bit-reverse/Hann tables once */
    cur_axis = axis;
    tdm_stream_cursor_init(&cursor);
    reset_window();
    ready = false;
    seq_counter = 0U;
}

void texture_set_axis(texture_axis_t axis)
{
    cur_axis = axis;
    reset_window();           /* don't mix axes within a window */
}

texture_axis_t texture_get_axis(void)
{
    return cur_axis;
}

static int16_t select_axis_sample(uint32_t i)
{
    switch (cur_axis) {
        case TEXTURE_AXIS_X: return pull_x[i];
        case TEXTURE_AXIS_Y: return pull_y[i];
        case TEXTURE_AXIS_Z: return pull_z[i];
        default:             return pull_z[i];
    }
}

/* Spectrum scratch (FFT_BINS floats). One window's magnitude spectrum. */
static float spectrum[FFT_BINS];

/* Bin -> frequency: bin k = k * Fs / FFT_N. With Fs=15625, FFT_N=256 that's
   ~61.04 Hz per bin. Band edges (Hz) -> bin indices:
     band0: 0-1000 Hz   -> bins 1..16   (skip bin 0 = DC)
     band1: 1000-2500   -> bins 17..40
     band2: 2500-5000   -> bins 41..81
     band3: 5000-7812   -> bins 82..128
   Energy is summed magnitude in each band; we scale down to fit uint16. */
#define BIN_HZ            (15625.0f / (float)FFT_N)
#define BAND0_LO   1U
#define BAND0_HI   16U
#define BAND1_LO   17U
#define BAND1_HI   40U
#define BAND2_LO   41U
#define BAND2_HI   81U
#define BAND3_LO   82U
#define BAND3_HI   (FFT_BINS - 1U)
/* Magnitudes can be large (sum over a window); scale band sums so typical
   values land in a useful uint16 range rather than saturating. Empirical;
   the classifier standardizes anyway so the exact scale isn't critical. */
#define BAND_SCALE 64.0f

static uint16_t clamp_u16_f(float v)
{
    if (v < 0.0f) {
        return 0U;
    }
    if (v > 65535.0f) {
        return 65535U;
    }
    return (uint16_t)(v + 0.5f);
}

static void compute_spectral_features(void)
{
    fft_real_magnitude(window, spectrum);

    float band[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float total = 0.0f;
    float weighted = 0.0f;       /* sum(freq * mag) for centroid */
    float peak_mag = 0.0f;
    uint32_t peak_bin = 1U;

    for (uint32_t k = 1U; k < FFT_BINS; k++) {   /* skip DC (bin 0) */
        float m = spectrum[k];
        float f = (float)k * BIN_HZ;

        total += m;
        weighted += f * m;

        if (m > peak_mag) {
            peak_mag = m;
            peak_bin = k;
        }

        if (k <= BAND0_HI)      { band[0] += m; }
        else if (k <= BAND1_HI) { band[1] += m; }
        else if (k <= BAND2_HI) { band[2] += m; }
        else                    { band[3] += m; }
    }

    latched.band0 = clamp_u16_f(band[0] / BAND_SCALE);
    latched.band1 = clamp_u16_f(band[1] / BAND_SCALE);
    latched.band2 = clamp_u16_f(band[2] / BAND_SCALE);
    latched.band3 = clamp_u16_f(band[3] / BAND_SCALE);

    float centroid = (total > 0.0f) ? (weighted / total) : 0.0f;
    latched.centroid = clamp_u16_f(centroid);
    latched.peakfreq = clamp_u16_f((float)peak_bin * BIN_HZ);
}

static void compute_features(void)
{
    /* Pass 1: mean. */
    int32_t sum = 0;
    for (uint32_t i = 0U; i < TEXTURE_WINDOW_SAMPLES; i++) {
        sum += window[i];
    }
    int32_t mean = sum / (int32_t)TEXTURE_WINDOW_SAMPLES;

    /* Pass 2: variance accumulator, MAD, peak, zero crossings (about mean). */
    uint64_t sq_sum = 0U;
    uint32_t abs_sum = 0U;
    uint32_t peak = 0U;
    uint32_t zcr = 0U;
    int32_t prev_centered = (int32_t)window[0] - mean;

    for (uint32_t i = 0U; i < TEXTURE_WINDOW_SAMPLES; i++) {
        int32_t c = (int32_t)window[i] - mean;
        uint32_t a = (c < 0) ? (uint32_t)(-c) : (uint32_t)c;

        sq_sum += (uint64_t)((int64_t)c * (int64_t)c);
        abs_sum += a;
        if (a > peak) {
            peak = a;
        }
        /* zero crossing: sign flip between consecutive centered samples.
           Treat exact-zero as no crossing to avoid double counting. */
        if (((prev_centered < 0) && (c > 0)) || ((prev_centered > 0) && (c < 0))) {
            zcr++;
        }
        prev_centered = c;
    }

    /* RMS = sqrt(mean of squares). Integer throughout: divide the sum of
       squares by N first, then integer-sqrt. */
    uint64_t meansq = sq_sum / (uint64_t)TEXTURE_WINDOW_SAMPLES;
    uint32_t rms = isqrt_u64(meansq);
    uint32_t mad = abs_sum / TEXTURE_WINDOW_SAMPLES;
    uint32_t crest = (rms > 0U) ? ((peak * 100U) / rms) : 0U;

    /* Clamp to the 16-bit fields (raw counts are int16, so centered magnitudes
       fit in 15 bits + a little; clamp defensively anyway). */
    latched.seq = seq_counter++;
    latched.mean = (int16_t)mean;
    latched.rms = (rms > 0xFFFFU) ? 0xFFFFU : (uint16_t)rms;
    latched.mad = (mad > 0xFFFFU) ? 0xFFFFU : (uint16_t)mad;
    latched.peak = (peak > 0xFFFFU) ? 0xFFFFU : (uint16_t)peak;
    latched.zcr = (zcr > 0xFFFFU) ? 0xFFFFU : (uint16_t)zcr;
    latched.crest = (crest > 0xFFFFU) ? 0xFFFFU : (uint16_t)crest;

    compute_spectral_features();

    ready = true;
}

void texture_update(void)
{
    /* Pull whatever new frames are available, in chunks, appending the chosen
       axis into window[]. Each filled window produces one feature vector. If
       multiple windows' worth arrived since the last call (shouldn't normally,
       but possible after a stall), we process them all so features stay in
       step with real time rather than lagging. */
    for (;;) {
        uint32_t room = TEXTURE_WINDOW_SAMPLES - fill;
        uint32_t want = (room < PULL_MAX) ? room : PULL_MAX;

        uint32_t got = tdm_stream_pull(&cursor, pull_x, pull_y, pull_z, want);
        if (got == 0U) {
            break;   /* nothing new right now */
        }

        for (uint32_t i = 0U; i < got; i++) {
            window[fill++] = select_axis_sample(i);

            if (fill >= TEXTURE_WINDOW_SAMPLES) {
                compute_features();
                fill = 0U;
            }
        }
    }
}

bool texture_take(texture_features_t *out)
{
    if (!ready || (out == NULL)) {
        return false;
    }
    *out = latched;
    ready = false;
    return true;
}
