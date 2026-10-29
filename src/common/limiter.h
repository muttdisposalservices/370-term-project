/* audiod's output limiter: brickwall peak, no lookahead (plan.md C11). It
 * runs on the mono Source signal after the EQ and before the mute gate, and
 * never depends on analyzerd or ctld being alive (ProposedStructure.md §5).
 *
 * Instant attack: a sample over the ceiling gets exactly the gain that puts
 * it on the ceiling, so the first such sample is clipped to c. That distorts
 * transients, which is accepted for driver protection (§4 allows no or
 * <= 1 ms lookahead). Release is a one-pole exponential in linear gain with
 * time constant LIMITER_RELEASE_MS.
 *
 * Non-finite input is passed through as NaN rather than hidden, so the mute
 * gate's check sees it: NaN stays NaN, and +-Inf comes out as NaN. gain
 * itself stays finite and within [0, 1] for any input, so there is no reset.
 * No allocation, no syscalls; the caller owns the struct. */
#ifndef LIMITER_H
#define LIMITER_H

#include <stddef.h>
#include <stdint.h>

#define LIMITER_DEFAULT_CEILING_DBFS (-6.0)
#define LIMITER_RELEASE_MS           50.0

struct limiter {
    double ceiling;   /* linear, (0, 1] */
    double gain;      /* gain applied to the last sample, [0, 1] */
    double release_k; /* per-sample release coefficient */
};

/* 1 if dbfs is finite and <= 0 (C11's param-block rule), else 0. */
int limiter_ceiling_valid(double dbfs);

/* Returns 0, or -EINVAL (l NULL, ceiling invalid, or rate 0). Starts at
 * unity gain. */
int limiter_init(struct limiter *l, double ceiling_dbfs, uint32_t rate);

/* Returns 0, or -EINVAL with the old ceiling kept. gain is untouched, so a
 * lower ceiling applies from the next sample and a higher one releases. */
int limiter_set_ceiling(struct limiter *l, double dbfs);

/* Limits buf (n samples) in place, so every finite output has |y| <= ceiling.
 * Returns the lowest gain applied in this block (the gain-reduction meter),
 * or the current gain if n is 0. */
double limiter_process(struct limiter *l, double *buf, size_t n);

#endif
