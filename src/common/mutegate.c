#include "mutegate.h"

#include <errno.h>
#include <math.h>
#include <string.h>

static void window_reset(struct mute_gate *g) {
    g->clean_frames = 0;
    g->dc_sum = 0.0;
}

int mute_gate_init(struct mute_gate *g, uint32_t rate) {
    if (!g)
        return -EINVAL;
    memset(g, 0, sizeof(*g));
    uint64_t need = (uint64_t)SELFCHECK_MS * rate / 1000u;
    uint64_t fade = (uint64_t)FADE_IN_MS * rate / 1000u;
    if (fade == 0 || need == 0 || need > UINT32_MAX)
        return -EINVAL;
    g->state = MUTE_MUTED;
    g->need_frames = (uint32_t)need;
    g->fade_frames = (uint32_t)fade;
    return 0;
}

void mute_gate_unlocked(struct mute_gate *g) {
    g->state = MUTE_MUTED;
    g->locked = 0;
    g->selfcheck_ok = 0;
    window_reset(g);
}

void mute_gate_locked(struct mute_gate *g) {
    g->locked = 1;
}

void mute_gate_params_seen(struct mute_gate *g) {
    g->params_seen = 1;
}

int mute_gate_check(struct mute_gate *g, const double *out, size_t n) {
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        if (!isfinite(out[i])) {
            g->state = MUTE_MUTED;
            g->selfcheck_ok = 0;
            window_reset(g);
            return 1;
        }
        sum += out[i];
    }

    if (g->state != MUTE_MUTED || !g->locked)
        return 0;

    /* A failed window clears selfcheck_ok as well as a passed one sets it. */
    g->clean_frames += (uint32_t)n;
    g->dc_sum += sum;
    if (g->clean_frames >= g->need_frames) {
        g->selfcheck_ok = fabs(g->dc_sum / (double)g->clean_frames) < DC_BOUND;
        window_reset(g);
    }
    if (g->selfcheck_ok && g->params_seen) {
        g->state = MUTE_FADING_IN;
        g->fade_pos = 0;
    }
    return 0;
}

void mute_gate_apply(struct mute_gate *g, double *out, size_t n) {
    switch (g->state) {
    case MUTE_MUTED:
        for (size_t i = 0; i < n; i++)
            out[i] = 0.0;
        break;
    case MUTE_FADING_IN:
        /* Gain fade_pos / fade_frames after the increment, so the last fade
         * sample gets exactly 1. */
        for (size_t i = 0; i < n && g->fade_pos < g->fade_frames; i++) {
            g->fade_pos++;
            out[i] *= (double)g->fade_pos / (double)g->fade_frames;
        }
        if (g->fade_pos >= g->fade_frames)
            g->state = MUTE_LIVE;
        break;
    case MUTE_LIVE:
        break;
    }
}

uint32_t mute_gate_reason(const struct mute_gate *g) {
    if (g->state != MUTE_MUTED)
        return 0;
    uint32_t r = 0;
    if (!g->locked)
        r |= MUTE_WAIT_LOCK;
    if (!g->selfcheck_ok)
        r |= MUTE_WAIT_SELFCHECK;
    if (!g->params_seen)
        r |= MUTE_WAIT_PARAMS;
    return r;
}
