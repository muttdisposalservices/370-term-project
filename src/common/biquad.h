/* audiod's EQ: a cascade of EQ_BANDS float64 biquads on the Source channel
 * (ProposedStructure.md §3, plan.md C11).
 *
 * Coefficients are normalised to a0 = 1:
 *   y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
 * Each band runs Direct Form I. Its state is only past inputs and outputs,
 * so the coefficients can change between samples (eq_process_ramp) without
 * a transient from internal state.
 *
 * struct eq_coefs is also the slot layout of the analyzerd -> audiod coeff
 * block. No allocation, no syscalls; the caller owns all state. */
#ifndef BIQUAD_H
#define BIQUAD_H

#include <stddef.h>

#define EQ_BANDS 19

struct biquad_coef {
    double b0, b1, b2, a1, a2;
};

struct eq_coefs {
    struct biquad_coef band[EQ_BANDS];
};

_Static_assert(sizeof(struct eq_coefs) == EQ_BANDS * 5 * sizeof(double),
               "coeff block slot must be 19 x 5 float64 (760 B)");

struct biquad_state {
    double x1, x2, y1, y2;
};

struct eq_state {
    struct biquad_state band[EQ_BANDS];
};

/* Every band unity (b0 = 1, the rest 0): audiod's compiled-in default. */
void eq_flat(struct eq_coefs *c);

/* 1 if every band has finite coefficients, is stable (|a2| < 1 and
 * |a1| < 1 + a2) and has a numerator that isn't all zero; else 0, including
 * for c NULL. The zero-numerator rule is what rejects a zeroed slot (C11). */
int eq_coefs_valid(const struct eq_coefs *c);

/* Zeroes the state. Call on every relock, and when a non-finite output trips
 * the mute gate: a NaN in y1/y2 would otherwise recirculate for good. */
void eq_reset(struct eq_state *s);

/* Filters buf (n samples) in place through every band. */
void eq_process(const struct eq_coefs *c, struct eq_state *s, double *buf, size_t n);

/* As eq_process, but each coefficient moves linearly from `from` to `to`
 * across the n samples: sample i uses from + (to - from) * (i + 1) / n, and
 * the last sample uses `to` exactly. Every point on the way is stable when
 * both ends are, since the stability triangle is convex. */
void eq_process_ramp(const struct eq_coefs *from, const struct eq_coefs *to,
                     struct eq_state *s, double *buf, size_t n);

#endif
