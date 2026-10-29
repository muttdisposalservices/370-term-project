#include "status.h"

#include <errno.h>
#include <math.h>
#include <string.h>

enum { M_SOURCE, M_SYSTEM, M_OUT, M_GAIN };

static void window_clear(double *v) {
    v[M_SOURCE] = 0.0;
    v[M_SYSTEM] = 0.0;
    v[M_OUT] = 0.0;
    v[M_GAIN] = 1.0;
}

static double max2(double a, double b) {
    return a > b ? a : b;
}

static double min2(double a, double b) {
    return a < b ? a : b;
}

int status_meter_init(struct status_meter *m, uint32_t rate) {
    if (!m)
        return -EINVAL;
    memset(m, 0, sizeof(*m));
    uint64_t w = (uint64_t)METER_WINDOW_MS * rate / 1000u;
    if (w == 0 || w > UINT32_MAX)
        return -EINVAL;
    m->window_frames = (uint32_t)w;
    window_clear(m->cur);
    window_clear(m->prev);
    return 0;
}

int status_meter_period(struct status_meter *m, const float *ring,
                        const double *out, size_t frames, double gain,
                        struct status_block *st) {
    double *c = m->cur;
    for (size_t i = 0; i < frames; i++) {
        c[M_SOURCE] = max2(c[M_SOURCE], fabs((double)ring[2 * i]));
        c[M_SYSTEM] = max2(c[M_SYSTEM], fabs((double)ring[2 * i + 1]));
        c[M_OUT] = max2(c[M_OUT], fabs(out[i]));
    }
    c[M_GAIN] = min2(c[M_GAIN], gain);

    /* Compared before adding, so a huge frames can't overflow the count. */
    if (frames < (size_t)(m->window_frames - m->frames)) {
        m->frames += (uint32_t)frames;
        return 0;
    }

    m->window++;
    st->meter_window = m->window;
    st->peak_source = max2(c[M_SOURCE], m->prev[M_SOURCE]);
    st->peak_system = max2(c[M_SYSTEM], m->prev[M_SYSTEM]);
    st->peak_out = max2(c[M_OUT], m->prev[M_OUT]);
    st->gain_min = min2(c[M_GAIN], m->prev[M_GAIN]);
    memcpy(m->prev, c, sizeof(m->prev));
    window_clear(c);
    m->frames = 0;
    return 1;
}
