#include "fft.h"

/* Self-contained: no <math.h> / libm dependency. The twiddle and Hann tables
   are built from sin/cos computed by a small polynomial approximation (good to
   ~1e-7 over the reduced range, far better than the int16 input precision
   needs), and magnitude uses an integer-free Newton sqrt. This keeps fft.c in
   line with the rest of the firmware, which links no external math library. */

#define FFT_PI  3.14159265358979323846f
#define FFT_2PI 6.28318530717958647692f

/* sinf/cosf via range reduction + polynomial. Reduces all the way into
   [-pi/2, pi/2] where the odd Taylor polynomial is very accurate, using
   sin(pi - x) = sin(x) for the outer quadrants. Accuracy ~1e-7, far beyond
   what the int16 sensor data needs. */
static float fsin_core(float x)
{
    /* x in [-pi/2, pi/2]. 9th-order odd Taylor. */
    float x2 = x * x;
    return x * (1.0f
              + x2 * (-1.0f / 6.0f
              + x2 * (1.0f / 120.0f
              + x2 * (-1.0f / 5040.0f
              + x2 * (1.0f / 362880.0f)))));
}

static float fsin(float x)
{
    /* reduce x into [-pi, pi] */
    while (x > FFT_PI)  { x -= FFT_2PI; }
    while (x < -FFT_PI) { x += FFT_2PI; }

    /* fold [-pi,-pi/2] and [pi/2,pi] into [-pi/2,pi/2] via sin(pi-x)=sin(x) */
    if (x > (FFT_PI * 0.5f)) {
        x = FFT_PI - x;
    } else if (x < -(FFT_PI * 0.5f)) {
        x = -FFT_PI - x;
    }
    return fsin_core(x);
}

static float fcos(float x)
{
    return fsin(x + (FFT_PI * 0.5f));
}

static float fsqrt(float v)
{
    if (v <= 0.0f) {
        return 0.0f;
    }
    /* Newton-Raphson; seed via bit trick on the exponent for fast convergence. */
    union { float f; uint32_t i; } u;
    u.f = v;
    u.i = (u.i >> 1) + 0x1FC00000U;   /* rough sqrt seed */
    float g = u.f;
    g = 0.5f * (g + v / g);
    g = 0.5f * (g + v / g);
    g = 0.5f * (g + v / g);
    g = 0.5f * (g + v / g);
    return g;
}

/* Precomputed tables (built once in fft_init). */
static float twiddle_re[FFT_N / 2U];
static float twiddle_im[FFT_N / 2U];
static uint16_t bitrev[FFT_N];
static float hann[FFT_N];
static int tables_ready;

/* Scratch buffers for one transform (real + imaginary working arrays). */
static float re_buf[FFT_N];
static float im_buf[FFT_N];

static uint16_t reverse_bits(uint16_t x, uint32_t nbits)
{
    uint16_t r = 0U;
    for (uint32_t i = 0U; i < nbits; i++) {
        r = (uint16_t)((r << 1) | (x & 1U));
        x >>= 1;
    }
    return r;
}

void fft_init(void)
{
    /* log2(FFT_N) */
    uint32_t nbits = 0U;
    for (uint32_t v = FFT_N; v > 1U; v >>= 1) {
        nbits++;
    }

    for (uint32_t i = 0U; i < FFT_N; i++) {
        bitrev[i] = reverse_bits((uint16_t)i, nbits);
    }

    /* Twiddle factors W_N^k = exp(-j 2pi k / N) for k = 0 .. N/2-1. */
    for (uint32_t k = 0U; k < (FFT_N / 2U); k++) {
        float ang = -2.0f * FFT_PI * (float)k / (float)FFT_N;
        twiddle_re[k] = fcos(ang);
        twiddle_im[k] = fsin(ang);
    }

    /* Hann window. */
    for (uint32_t i = 0U; i < FFT_N; i++) {
        hann[i] = 0.5f - 0.5f * fcos(2.0f * FFT_PI * (float)i / (float)(FFT_N - 1U));
    }

    tables_ready = 1;
}

void fft_real_magnitude(const int16_t *in, float *mag)
{
    if (!tables_ready) {
        fft_init();
    }

    /* Load bit-reversed, Hann-windowed, mean-removed real input; imag = 0.
       Remove DC first so the window's gravity/offset doesn't dominate bin 0
       and leak. */
    int32_t sum = 0;
    for (uint32_t i = 0U; i < FFT_N; i++) {
        sum += in[i];
    }
    float mean = (float)sum / (float)FFT_N;

    for (uint32_t i = 0U; i < FFT_N; i++) {
        uint16_t src = bitrev[i];
        float windowed = ((float)in[src] - mean) * hann[src];
        re_buf[i] = windowed;
        im_buf[i] = 0.0f;
    }

    /* Iterative radix-2 DIT butterflies. */
    for (uint32_t length = 2U; length <= FFT_N; length <<= 1) {
        uint32_t half = length >> 1;
        uint32_t step = FFT_N / length;   /* twiddle index stride */

        for (uint32_t start = 0U; start < FFT_N; start += length) {
            uint32_t tw = 0U;
            for (uint32_t k = 0U; k < half; k++) {
                uint32_t a = start + k;
                uint32_t b = a + half;

                float wr = twiddle_re[tw];
                float wi = twiddle_im[tw];

                float tr = wr * re_buf[b] - wi * im_buf[b];
                float ti = wr * im_buf[b] + wi * re_buf[b];

                re_buf[b] = re_buf[a] - tr;
                im_buf[b] = im_buf[a] - ti;
                re_buf[a] = re_buf[a] + tr;
                im_buf[a] = im_buf[a] + ti;

                tw += step;
            }
        }
    }

    /* Magnitude for bins 0..N/2. */
    for (uint32_t k = 0U; k < FFT_BINS; k++) {
        mag[k] = fsqrt(re_buf[k] * re_buf[k] + im_buf[k] * im_buf[k]);
    }
}
