/* The audiod -> ctld status block (ProposedStructure.md §3, issues.md C10).
 * struct status_block is the payload behind the seqlock in /dspunit-status:
 * audiod fills a private copy every period and seqlock_writes it; ctld
 * samples it at 20 Hz and audiod's watchdog thread reads the heartbeat.
 *
 * Every field is fixed-width and naturally aligned, so the layout has no
 * padding and is the same in every daemon. seqlock_init zeroes the data,
 * and a zeroed block reads as "nothing written yet": heartbeat 0,
 * meter_window 0, mute_state MUTE_MUTED.
 *
 * Meters are linear (no log10 on the RT thread; ctld converts to dB) and
 * cover windows, not single periods, so a 20 Hz reader doesn't miss
 * transients. Each publish covers the last two completed METER_WINDOW_MS
 * windows, so any two reads under ~100 ms apart see every sample at least
 * once. meter_window advances by one per window, so a reader can tell a
 * skipped or repeated window.
 *
 * status_meter builds the meter fields. No allocation, no syscalls; the
 * caller owns the struct. The other fields are plain stores by audiod. */
#ifndef STATUS_H
#define STATUS_H

#include <stddef.h>
#include <stdint.h>

#include "mutegate.h"

#define METER_WINDOW_MS 50u

struct status_block {
    uint64_t heartbeat;      /* every RT-loop iteration, settle/relock too (stall.h) */
    uint64_t xruns;          /* since this audiod started */
    uint64_t rejected_coeff; /* coeff blocks that failed eq_coefs_valid */
    uint64_t rejected_param; /* param blocks that failed validation */
    uint64_t meter_window;   /* last completed window, first = 1; 0 = no meters yet */
    double peak_source;      /* max |x| of capture L, full scale 1 */
    double peak_system;      /* max |x| of capture R (mic) */
    double peak_out;         /* max |x| of playback, after the mute gate */
    double gain_min;         /* lowest limiter gain, [0, 1]; 1 = no reduction */
    uint32_t mute_state;     /* enum mute_state; a u32 since enum size varies */
    uint32_t mute_reason;    /* MUTE_WAIT_* bits */
};

_Static_assert(sizeof(struct status_block) == 80,
               "status block must have no padding (80 B)");

struct status_meter {
    uint32_t window_frames; /* METER_WINDOW_MS at rate */
    uint32_t frames;        /* frames in the current window */
    uint64_t window;        /* completed windows */
    double cur[4], prev[4]; /* peak_source, peak_system, peak_out, gain_min */
};

/* Returns 0, or -EINVAL (m NULL, or rate too low for a 1-frame window). */
int status_meter_init(struct status_meter *m, uint32_t rate);

/* Folds one period into the current window. ring is the period's
 * interleaved L/R float32 capture, as pcm_capture wrote it; out is the mono
 * playback after mute_gate_apply; gain is limiter_process's return.
 *
 * Windows close on period boundaries: on the first period that brings the
 * window to window_frames, st's meter fields get meter_window and the
 * values over this window and the one before, a new window starts and 1 is
 * returned. Otherwise returns 0 and st is untouched.
 *
 * A NaN is ignored by the comparisons. None should arrive: capture is
 * integer PCM, a muted period is exact zeros, and the limiter gain is
 * always finite. */
int status_meter_period(struct status_meter *m, const float *ring,
                        const double *out, size_t frames, double gain,
                        struct status_block *st);

#endif
