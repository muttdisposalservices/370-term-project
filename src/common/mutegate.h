/* audiod's startup mute gate (plan.md C11, ProposedStructure.md §5).
 *
 * MUTED -> FADING_IN -> LIVE once all three hold:
 *   (a) locked:       both PCMs locked through the C4 start sequence;
 *   (b) selfcheck_ok: the last SELFCHECK_MS window of processed output was
 *                     clean: every sample finite, |mean| < DC_BOUND;
 *   (c) params_seen:  one valid ctld param block since this audiod started.
 * While MUTED and locked the self-check re-runs every window, so the unmute
 * is judged on the most recent window however long ctld takes to arrive.
 *
 * Per period, in this order:
 *   mute_gate_check(out)   after the limiter; 1 means non-finite: eq_reset
 *   mute_gate_apply(out)   then pcm_playback
 * State changes only in mute_gate_check, once per period. The event calls
 * (unlocked, locked, params_seen) only set flags.
 *
 * Re-muting (xrun or non-finite output) is a step to 0, not a fade: the
 * no-step rule is for unmuting. A muted period is written as exact zeros,
 * not multiplied by 0, so a NaN never reaches playback.
 * No allocation, no syscalls; the caller owns the struct. */
#ifndef MUTEGATE_H
#define MUTEGATE_H

#include <stddef.h>
#include <stdint.h>

#define SELFCHECK_MS 250u /* H3 resolved at ~0ms settle (issues.md H3); 250ms
                              not blocked on it, fine as final */
#define FADE_IN_MS   20u
#define DC_BOUND     0.01 /* -40 dBFS */

enum mute_state { MUTE_MUTED, MUTE_FADING_IN, MUTE_LIVE };

/* mute_reason bits for the status block. */
#define MUTE_WAIT_LOCK      1u
#define MUTE_WAIT_SELFCHECK 2u
#define MUTE_WAIT_PARAMS    4u

struct mute_gate {
    enum mute_state state;
    int locked;
    int selfcheck_ok;
    int params_seen;      /* latched for the life of the process */
    uint32_t need_frames; /* SELFCHECK_MS at rate */
    uint32_t fade_frames; /* FADE_IN_MS at rate */
    uint32_t clean_frames;
    uint32_t fade_pos;
    double dc_sum;
};

/* Returns 0, or -EINVAL (g NULL, or rate too low for a whole fade frame).
 * Starts MUTED with all three conditions unmet. */
int mute_gate_init(struct mute_gate *g, uint32_t rate);

/* An xrun, before the relock: MUTED, and (a) and (b) start over. (c) stays. */
void mute_gate_unlocked(struct mute_gate *g);

/* The C4 start sequence finished: (a) holds. */
void mute_gate_locked(struct mute_gate *g);

/* The first valid ctld param block was accepted: (c) holds. Idempotent. */
void mute_gate_params_seen(struct mute_gate *g);

/* Checks one period of limiter output. Returns 1 if any sample is
 * non-finite, which re-mutes in this same period and restarts (b); else 0. */
int mute_gate_check(struct mute_gate *g, const double *out, size_t n);

/* Applies the gate's gain to out in place: 0 when MUTED, a linear ramp
 * across periods when FADING_IN (ending exactly at 1, then LIVE), and
 * unchanged when LIVE. */
void mute_gate_apply(struct mute_gate *g, double *out, size_t n);

/* MUTE_WAIT_* bits for whichever conditions are unmet, or 0 unless MUTED. */
uint32_t mute_gate_reason(const struct mute_gate *g);

#endif
