#ifndef TDM_H
#define TDM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TDM_STATUS_DISABLED = 0,
    TDM_STATUS_STREAMING,
    TDM_STATUS_ERROR,
} tdm_status_t;

void tdm_init(void);
void tdm_update(void);
tdm_status_t tdm_status(void);

/* Running count of TDM frames (one Z,Y,X triplet each) the DMA has captured
   since init. If this advances between heartbeats, the TDM link is live. */
uint32_t tdm_frame_count(void);

/* Most recent complete audio-accelerometer frame, raw 16-bit per axis.
   Returns false if no frame has been captured yet. */
bool tdm_latest_xyz(int16_t *x, int16_t *y, int16_t *z);

/* DIAGNOSTIC: copy the most recent 8-slot frame (raw 16-bit per slot). Only
   slots 0-2 are meaningful in normal 3-slot operation; kept for re-diagnosing
   TDM bring-up issues if they ever recur. */
bool tdm_dump_frame(int16_t out[8]);

/* --------------------------- capture-burst API --------------------------- *
 * One-shot full-rate 3-axis capture into RAM (~0.3 s at the measured ~15.6
 * kHz frame rate, sized to fit free RAM -- see tdm.c for the budget math),
 * for later slow drain over UART. Call tdm_capture_start() to arm; poll
 * tdm_capture_active()/tdm_capture_ready() from the main loop; once ready,
 * read samples out with tdm_capture_get() and print them at whatever pace
 * the UART can sustain. tdm_update() must keep being called regularly while
 * a capture is active (see the timing note in tdm.c).
 * -------------------------------------------------------------------------- */

/* Arms a new capture. No-op if TDM isn't streaming. Overwrites any previous
   capture. */
void tdm_capture_start(void);

/* True while a capture is in progress (buffer not yet full). */
bool tdm_capture_active(void);

/* True once a capture has finished and is ready to be drained. */
bool tdm_capture_ready(void);

/* Number of frames captured so far (== tdm_capture_total_frames() once ready). */
uint32_t tdm_capture_count(void);

/* Total frames a full capture holds (fixed capacity). */
uint32_t tdm_capture_total_frames(void);

/* Reads back one captured frame by index (0 .. tdm_capture_count()-1). Returns
   false if index is out of range. */
bool tdm_capture_get(uint32_t index, int16_t *x, int16_t *y, int16_t *z);

/* --------------------------- continuous stream --------------------------- *
 * For always-on consumers (e.g. the texture feature extractor) that need
 * every frame, not just the latest. tdm_stream_pull() copies up to max_frames
 * newly-arrived frames (since the previous pull) into the caller's arrays and
 * returns how many it wrote. Call it often enough that fewer than TDM_FRAMES
 * (128, ~8.2 ms worth) accumulate between calls, or the oldest unread frames
 * are overwritten in the circular buffer and silently lost -- the return value
 * vs. how many you expected lets you detect that. x/y/z may be NULL if an axis
 * isn't needed. Pass a cursor owned by the caller, initialized with
 * tdm_stream_cursor_init().
 * ------------------------------------------------------------------------- */
typedef struct {
    uint32_t next_frame;   /* next absolute frame index this consumer wants */
    bool     primed;       /* false until first pull establishes the start  */
} tdm_stream_cursor_t;

/* Initializes a stream cursor. The first tdm_stream_pull() after this starts
   from the most recent frame (i.e. it does not try to replay old history). */
void tdm_stream_cursor_init(tdm_stream_cursor_t *cursor);

/* Copies newly-arrived frames into x/y/z (each may be NULL), up to max_frames.
   Returns the number of frames written (0 if none new). Updates the cursor. */
uint32_t tdm_stream_pull(tdm_stream_cursor_t *cursor,
                         int16_t *x, int16_t *y, int16_t *z,
                         uint32_t max_frames);

#endif
