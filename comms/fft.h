#ifndef FFT_H
#define FFT_H

#include <stdint.h>

/* -------------------------------------------------------------------------- *
 * Minimal radix-2 FFT for real input, bare-metal (no CMSIS-DSP, no libm at
 * runtime beyond what's inlined). Fixed size FFT_N (power of two). Uses float
 * (the G431 has a single-precision FPU, -mfpu=fpv4-sp-d16), validated against
 * NumPy to ~1e-11 magnitude error.
 *
 * Usage:
 *   fft_init();                       // once, builds twiddle + bit-reverse tables
 *   fft_real_magnitude(window, mag);  // window[FFT_N] -> mag[FFT_N/2 + 1]
 *
 * mag[k] is the magnitude of frequency bin k, where bin k corresponds to
 * frequency k * Fs / FFT_N. With Fs = 15625 Hz and FFT_N = 256, bin spacing
 * is ~61.0 Hz, Nyquist (bin 128) = ~7812 Hz.
 * -------------------------------------------------------------------------- */

#define FFT_N        256U
#define FFT_BINS     (FFT_N / 2U + 1U)   /* 0..Nyquist inclusive */

/* Build internal tables. Call once at startup before fft_real_magnitude(). */
void fft_init(void);

/* Compute the magnitude spectrum of a real window.
   in:  pointer to FFT_N real samples (int16, e.g. one axis of a window)
   mag: output, FFT_BINS floats (bin 0 = DC ... bin FFT_N/2 = Nyquist)
   A Hann window is applied internally to reduce spectral leakage. */
void fft_real_magnitude(const int16_t *in, float *mag);

#endif
