#include "stall.h"

#include <string.h>

#define STALL_NS ((uint64_t)STALL_MS * 1000000u)

void stall_init(struct stall *s) {
    memset(s, 0, sizeof(*s));
}

int stall_check(struct stall *s, uint64_t hb, uint64_t now_ns, uint64_t *age_ns) {
    uint64_t age = 0;
    int stalled = 0;

    if (!s->have_hb) {
        s->last_hb = hb;
        s->have_hb = 1;
    } else if (hb != s->last_hb) {
        s->last_hb = hb;
        s->last_change_ns = now_ns;
        s->armed = 1;
    } else if (s->armed) {
        /* Monotonic time can't go backwards; if it somehow does, age 0. */
        age = now_ns > s->last_change_ns ? now_ns - s->last_change_ns : 0;
        stalled = age >= STALL_NS;
    }

    if (age_ns)
        *age_ns = age;
    return stalled;
}
