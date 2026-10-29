#include "pace.h"

#include <errno.h>

#define NS_PER_S 1000000000ull

int pace_init(struct pace *p, uint32_t rate, uint32_t period_frames, uint64_t start_ns) {
    if (!p || rate == 0 || period_frames == 0)
        return -EINVAL;
    uint64_t period_ns = (uint64_t)period_frames * NS_PER_S / rate;
    if (period_ns <= PACE_MARGIN_NS)
        return -EINVAL;
    p->anchor_ns = start_ns;
    p->period_ns = period_ns;
    p->rate = rate;
    p->period_frames = period_frames;
    return 0;
}

uint64_t pace_poll_from(const struct pace *p) {
    return p->anchor_ns + p->period_ns - PACE_MARGIN_NS;
}

void pace_reanchor(struct pace *p, uint64_t now_ns, uint64_t avail) {
    uint64_t back = 0;
    if (avail > p->period_frames) {
        uint64_t excess = avail - p->period_frames;
        /* Integer math: a 96 kHz frame isn't a whole number of ns. An excess
         * too large to scale is far past any real buffer: anchor at 0. */
        back = excess > UINT64_MAX / NS_PER_S ? UINT64_MAX : excess * NS_PER_S / p->rate;
    }
    p->anchor_ns = back < now_ns ? now_ns - back : 0;
}
