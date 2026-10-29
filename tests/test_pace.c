/* Period-boundary prediction (src/common/pace.c, plan.md F1). */
#include "test.h"
#include "pace.h"

#include <errno.h>
#include <stdint.h>

#define RATE      96000u
#define T0        1e12   /* ns: stream start, well after boot */
#define POLL_NS   1000.0 /* one snd_pcm_avail_update per us once polling */
#define WORK_NS   20000u /* processing one period */
#define RING      1024u
#define PERIODS   200000u /* 100 s of audio: 20 ms of drift at 200 ppm */

/* Simulated hardware pointer. Chunk i (of `chunk` frames) completes at
 * T0 + i * chunk_ns and becomes visible up to jitter_ns later, in order.
 * g_vt[i % RING] is its visible time, generated on demand. */
static double g_vt[RING];
static uint64_t g_gen;
static double g_chunk_ns, g_jitter_ns;
static uint64_t g_rng;

static double jitter(void) {
    g_rng = g_rng * 6364136223846793005u + 1442695040888963407u;
    return (double)(g_rng >> 11) * 0x1p-53 * g_jitter_ns;
}

static double vis_time(uint64_t i) {
    while (g_gen < i) {
        g_gen++;
        double v = T0 + (double)g_gen * g_chunk_ns + jitter();
        double prev = g_vt[(g_gen - 1) % RING];
        g_vt[g_gen % RING] = v > prev ? v : prev;
    }
    return g_vt[i % RING];
}

struct sim {
    double drift_ppm; /* audio clock vs monotonic; + is fast */
    uint32_t chunk;   /* frames per hardware-pointer step; 1 = smooth */
    double jitter_ns; /* IRQ visibility jitter */
    size_t delay_at;  /* period at which the loop is held up (0: never) */
    uint64_t delay_ns;
};

/* Runs the RT loop's wait against the simulated pointer and checks every
 * period:
 *   - the poll window never opens after the period becomes visible;
 *   - it opens at most margin + jitter + 2 us early (bounded polling);
 *   - when the loop was ready in time, it sees the period within one poll.
 * Returns how many periods the loop only reached after they were visible. */
static size_t simulate(const struct sim *c, uint32_t n) {
    struct pace p;
    g_chunk_ns = c->chunk * 1e9 / (RATE * (1.0 + c->drift_ppm * 1e-6));
    g_jitter_ns = c->jitter_ns;
    g_gen = 0;
    g_vt[0] = T0;
    g_rng = 0x9e3779b97f4a7c15u;

    TEST_EQ(pace_init(&p, RATE, n, (uint64_t)T0), 0);
    uint64_t t = (uint64_t)T0;
    uint64_t consumed = 0;
    size_t late = 0, bad_open = 0, bad_early = 0, bad_seen = 0;

    for (size_t k = 1; k <= PERIODS; k++) {
        uint64_t w = pace_poll_from(&p);
        if (t < w)
            t = w; /* the clock spin */
        if (k == c->delay_at)
            t += c->delay_ns; /* held up just as polling would start */
        uint64_t ready = t;

        /* The period is visible once the chunk holding its last frame is. */
        uint64_t need = (consumed + n + c->chunk - 1) / c->chunk;
        double pv = vis_time(need);
        if ((double)t < pv)
            t += (uint64_t)(ceil((pv - (double)t) / POLL_NS) * POLL_NS);

        if ((double)w > pv)
            bad_open++;
        if (pv - (double)w > PACE_MARGIN_NS + c->jitter_ns + 2000.0)
            bad_early++;
        if ((double)ready <= pv) {
            if ((double)t - pv >= POLL_NS)
                bad_seen++;
        } else {
            late++;
        }

        uint64_t last = need;
        while (vis_time(last + 1) <= (double)t)
            last++;
        pace_reanchor(&p, t, last * c->chunk - consumed);
        consumed += n;
        t += WORK_NS;
    }
    TEST_EQ(bad_open, 0u);
    TEST_EQ(bad_early, 0u);
    TEST_EQ(bad_seen, 0u);
    return late;
}

static void test_init(void) {
    struct pace p;
    TEST_EQ(pace_init(NULL, RATE, 48, 0), -EINVAL);
    TEST_EQ(pace_init(&p, 0, 48, 0), -EINVAL);
    TEST_EQ(pace_init(&p, RATE, 0, 0), -EINVAL);
    TEST_EQ(pace_init(&p, RATE, 4, 0), -EINVAL); /* 41.7 us <= margin */

    TEST_EQ(pace_init(&p, RATE, 48, 7000000), 0);
    TEST_EQ(p.period_ns, 500000u);
    TEST_EQ(pace_poll_from(&p), 7000000u + 500000u - PACE_MARGIN_NS);
    TEST_EQ(pace_init(&p, RATE, 96, 7000000), 0);
    TEST_EQ(p.period_ns, 1000000u);
}

static void test_reanchor(void) {
    struct pace p;
    TEST_EQ(pace_init(&p, RATE, 48, 0), 0);

    pace_reanchor(&p, 9000000, 48);
    TEST_EQ(p.anchor_ns, 9000000u);
    pace_reanchor(&p, 9000000, 96); /* one whole period past the boundary */
    TEST_EQ(p.anchor_ns, 9000000u - 500000u);
    pace_reanchor(&p, 9000000, 49); /* one frame: 10416.67 ns, floored */
    TEST_EQ(p.anchor_ns, 9000000u - 10416u);
    pace_reanchor(&p, 9000000, 10); /* below a period: no backdate */
    TEST_EQ(p.anchor_ns, 9000000u);

    pace_reanchor(&p, 1000, 4800); /* backdate past zero */
    TEST_EQ(p.anchor_ns, 0u);
    pace_reanchor(&p, 1000, UINT64_MAX);
    TEST_EQ(p.anchor_ns, 0u);
}

/* No drift or +-200 ppm, smooth pointer: always on time, never late. */
static void test_drift(void) {
    static const double ppm[] = {0.0, 200.0, -200.0};
    for (size_t i = 0; i < 3; i++) {
        struct sim c = {ppm[i], 1, 0.0, 0, 0};
        TEST_EQ(simulate(&c, 48), 0u);
        TEST_EQ(simulate(&c, 96), 0u);
    }
}

/* One 300 us hold-up: that period is seen late, the next window is on time. */
static void test_late_detection(void) {
    struct sim c = {150.0, 1, 0.0, 1000, 300000};
    TEST_EQ(simulate(&c, 48), 1u);
}

/* A 700 us hold-up leaves two periods pending: the second is processed at
 * once (its window is already past), and the one after is on time. */
static void test_catch_up(void) {
    struct sim c = {-150.0, 1, 0.0, 2000, 700000};
    TEST_EQ(simulate(&c, 48), 2u);
}

/* USB-style pointer: 12-frame (one microframe) steps with up to 30 us of
 * IRQ jitter, under the 50 us margin. */
static void test_chunks_and_jitter(void) {
    struct sim c = {200.0, 12, 30000.0, 0, 0};
    TEST_EQ(simulate(&c, 48), 0u);
    TEST_EQ(simulate(&c, 96), 0u);
    c.drift_ppm = -200.0;
    TEST_EQ(simulate(&c, 48), 0u);
}

int main(void) {
    test_init();
    test_reanchor();
    test_drift();
    test_late_detection();
    test_catch_up();
    test_chunks_and_jitter();
    return test_failures();
}
