#include "biquad.h"

#include <math.h>
#include <string.h>

void eq_flat(struct eq_coefs *c) {
    memset(c, 0, sizeof(*c));
    for (size_t b = 0; b < EQ_BANDS; b++)
        c->band[b].b0 = 1.0;
}

int eq_coefs_valid(const struct eq_coefs *c) {
    if (!c)
        return 0;
    for (size_t b = 0; b < EQ_BANDS; b++) {
        const struct biquad_coef *k = &c->band[b];
        if (!isfinite(k->b0) || !isfinite(k->b1) || !isfinite(k->b2) ||
            !isfinite(k->a1) || !isfinite(k->a2))
            return 0;
        if (!(fabs(k->a2) < 1.0 && fabs(k->a1) < 1.0 + k->a2))
            return 0;
        if (k->b0 == 0.0 && k->b1 == 0.0 && k->b2 == 0.0)
            return 0;
    }
    return 1;
}

void eq_reset(struct eq_state *s) {
    memset(s, 0, sizeof(*s));
}

/* One sample through one Direct Form I section. */
static inline double df1(const struct biquad_coef *k, struct biquad_state *st, double x) {
    double y = k->b0 * x + k->b1 * st->x1 + k->b2 * st->x2 - k->a1 * st->y1 - k->a2 * st->y2;
    st->x2 = st->x1;
    st->x1 = x;
    st->y2 = st->y1;
    st->y1 = y;
    return y;
}

void eq_process(const struct eq_coefs *c, struct eq_state *s, double *buf, size_t n) {
    /* Band by band over the whole buffer: the same arithmetic, in the same
     * order per sample, as sample by sample through the cascade. */
    for (size_t b = 0; b < EQ_BANDS; b++) {
        const struct biquad_coef *k = &c->band[b];
        struct biquad_state st = s->band[b];
        for (size_t i = 0; i < n; i++)
            buf[i] = df1(k, &st, buf[i]);
        s->band[b] = st;
    }
}

void eq_process_ramp(const struct eq_coefs *from, const struct eq_coefs *to,
                     struct eq_state *s, double *buf, size_t n) {
    for (size_t b = 0; b < EQ_BANDS; b++) {
        const struct biquad_coef *f = &from->band[b];
        const struct biquad_coef *t = &to->band[b];
        struct biquad_coef d = {t->b0 - f->b0, t->b1 - f->b1, t->b2 - f->b2,
                                t->a1 - f->a1, t->a2 - f->a2};
        struct biquad_state st = s->band[b];
        /* from + d * u is exactly `from` when the ends match; the last sample
         * takes `to` directly, since from + d may differ from to by rounding. */
        for (size_t i = 0; i + 1 < n; i++) {
            double u = (double)(i + 1) / (double)n;
            struct biquad_coef k = {f->b0 + d.b0 * u, f->b1 + d.b1 * u, f->b2 + d.b2 * u,
                                    f->a1 + d.a1 * u, f->a2 + d.a2 * u};
            buf[i] = df1(&k, &st, buf[i]);
        }
        if (n > 0)
            buf[n - 1] = df1(t, &st, buf[n - 1]);
        s->band[b] = st;
    }
}
