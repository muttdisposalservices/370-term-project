/* Period-boundary prediction for audiod's time-gated poll (plan.md F1).
 *
 * The RT loop spins on CLOCK_MONOTONIC (vDSO, no syscall) until
 * pace_poll_from(), about PACE_MARGIN_NS before the predicted boundary, and
 * only then polls snd_pcm_avail_update() on the capture PCM. On arm64 each
 * of those is a real ioctl (SNDRV_PCM_IOCTL_SYNC_PTR) that takes the stream
 * lock the xHCI IRQ thread also takes -- confirmed on hardware (issues.md
 * F1), so this keeps it to a few calls per period, not continuous.
 *
 * The prediction is re-anchored at every observed boundary, so drift
 * between the monotonic and audio clocks never carries past one period.
 * Frames already past the boundary when it's seen backdate the anchor, so
 * a late detection (a delayed loop, or several periods pending) doesn't
 * push the next window late. With a chunked hardware pointer the backdate
 * can over-correct slightly, which only opens the window early.
 * No allocation, no syscalls; the caller owns the struct. */
#ifndef PACE_H
#define PACE_H

#include <stdint.h>

#define PACE_MARGIN_NS 50000u /* proposed margin; F1 (issues.md) confirmed the
                                  ioctl-per-check mechanism this margin covers,
                                  not a measured value itself */

struct pace {
    uint64_t anchor_ns; /* estimated time of the last period boundary */
    uint64_t period_ns;
    uint32_t rate;
    uint32_t period_frames;
};

/* start_ns: when snd_pcm_start(capture) returned. Call again on every
 * relock. Returns 0, or -EINVAL (p NULL, rate or period_frames 0, or a
 * period no longer than PACE_MARGIN_NS). */
int pace_init(struct pace *p, uint32_t rate, uint32_t period_frames, uint64_t start_ns);

/* When to stop spinning on the clock and start polling. May already be in
 * the past, after a late period; then poll at once. */
uint64_t pace_poll_from(const struct pace *p);

/* Called once avail >= period_frames, with the time that avail was read.
 * The boundary was (avail - period_frames) frames before now_ns. */
void pace_reanchor(struct pace *p, uint64_t now_ns, uint64_t avail);

#endif
