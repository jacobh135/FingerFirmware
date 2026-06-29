#ifndef TEXTURE_H
#define TEXTURE_H

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- *
 * Phase 1 texture sensing: continuous time-domain feature extraction.
 *
 * Consumes the live TDM audio-accelerometer stream (via tdm.c), accumulates
 * samples from one axis into fixed-size windows, and computes a small set of
 * time-domain features per window -- no FFT. One feature vector is produced
 * per window (~16 ms at the ~15.6 kHz frame rate, ~61 windows/s), small enough
 * to stream continuously over UART now and over CAN/CAN-FD later.
 *
 * The features are deliberately generic signal descriptors, NOT texture
 * decisions -- classification happens off-board ("the brain elsewhere").
 * -------------------------------------------------------------------------- */

#define TEXTURE_WINDOW_SAMPLES 256U   /* ~16.4 ms per window at 15625 Hz */

typedef enum {
    TEXTURE_AXIS_X = 0,
    TEXTURE_AXIS_Y,
    TEXTURE_AXIS_Z,
} texture_axis_t;

typedef struct {
    uint32_t seq;        /* window sequence number since init (monotonic)      */
    /* time-domain (Phase 1) */
    uint16_t rms;        /* RMS of the DC-removed window (counts)              */
    uint16_t mad;        /* mean absolute deviation from the window mean       */
    uint16_t peak;       /* max |sample - mean| in the window                 */
    uint16_t zcr;        /* zero-crossing count (about the window mean)        */
    uint16_t crest;      /* peak/rms ratio x100 (integer; 0 if rms==0)        */
    int16_t  mean;       /* window DC mean (mostly gravity+bias on this axis) */
    /* frequency-domain (Phase 2) */
    uint16_t band0;      /* spectral energy 0-1000 Hz   (scaled, see note)    */
    uint16_t band1;      /* spectral energy 1000-2500 Hz                      */
    uint16_t band2;      /* spectral energy 2500-5000 Hz                      */
    uint16_t band3;      /* spectral energy 5000-7812 Hz                      */
    uint16_t centroid;   /* spectral centroid in Hz (energy-weighted mean)    */
    uint16_t peakfreq;   /* frequency (Hz) of the strongest non-DC bin        */
} texture_features_t;

/* Initialize the texture layer. Does not touch the TDM hardware -- call
   tdm_init() first. Selects which axis to analyze. */
void texture_init(texture_axis_t axis);

/* Pump the feature extractor. Call every main-loop iteration (same cadence as
   tdm_update(), and after it). Pulls any newly-captured frames from the live
   TDM buffer, appends the selected axis into the current window, and when a
   window fills, computes its features and makes them available via
   texture_take(). Non-blocking. */
void texture_update(void);

/* If a new completed window's features are ready, copies them into *out and
   returns true (clearing the ready flag). Returns false if nothing new since
   the last call. Typical use: poll this in the main loop and print/emit the
   vector whenever it returns true. */
bool texture_take(texture_features_t *out);

/* Change the analyzed axis at runtime (resets the in-progress window). */
void texture_set_axis(texture_axis_t axis);

/* Current axis. */
texture_axis_t texture_get_axis(void);

#endif
