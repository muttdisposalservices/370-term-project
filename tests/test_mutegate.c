/* Startup mute gate (src/common/mutegate.c, plan.md C11). */
#include "test.h"
#include "mutegate.h"

#include <errno.h>
#include <string.h>

#define RATE   96000u
#define P48    48u
#define WIN48  500u  /* SELFCHECK_MS of 48-frame periods */
#define FADE   1920u /* FADE_IN_MS at RATE */
#define TWO_PI 6.28318530717958647692

#define ALL_WAIT (MUTE_WAIT_LOCK | MUTE_WAIT_SELFCHECK | MUTE_WAIT_PARAMS)

/* Test signal: a constant level, or a sine when g_freq != 0. g_t counts
 * frames since the signal started, so phase runs across periods. */
static double g_level, g_freq, g_amp;
static size_t g_t;

static double sample(void) {
    double t = (double)g_t++ / RATE;
    return g_freq != 0.0 ? g_amp * sin(TWO_PI * g_freq * t) : g_level;
}

static void set_level(double v) {
    g_level = v;
    g_freq = 0.0;
    g_t = 0;
}

static void set_sine(double freq, double amp) {
    g_freq = freq;
    g_amp = amp;
    g_t = 0;
}

/* Runs up to `periods` periods of n frames through check and apply. Returns
 * the 1-based period in which the gate left MUTED (and stops there), else 0. */
static size_t feed(struct mute_gate *g, size_t n, size_t periods) {
    double buf[96];
    for (size_t p = 1; p <= periods; p++) {
        for (size_t i = 0; i < n; i++)
            buf[i] = sample();
        int was_muted = g->state == MUTE_MUTED;
        TEST_EQ(mute_gate_check(g, buf, n), 0);
        mute_gate_apply(g, buf, n);
        if (was_muted && g->state != MUTE_MUTED)
            return p;
    }
    return 0;
}

/* A gate that is LIVE: locked, params seen, one clean window, full fade. */
static void make_live(struct mute_gate *g) {
    TEST_EQ(mute_gate_init(g, RATE), 0);
    mute_gate_locked(g);
    mute_gate_params_seen(g);
    set_level(0.0);
    TEST_EQ(feed(g, P48, WIN48), WIN48);
    feed(g, P48, FADE / P48);
    TEST_EQ(g->state, MUTE_LIVE);
}

static void test_init(void) {
    struct mute_gate g;
    TEST_EQ(mute_gate_init(NULL, RATE), -EINVAL);
    TEST_EQ(mute_gate_init(&g, 0), -EINVAL);
    TEST_EQ(mute_gate_init(&g, RATE), 0);
    TEST_EQ(g.state, MUTE_MUTED);
    TEST_EQ(g.need_frames, 24000u);
    TEST_EQ(g.fade_frames, FADE);
    TEST_EQ(mute_gate_reason(&g), ALL_WAIT);
}

/* The self-check can't pass before the lock, so there are three orders.
 * Each unmutes on the exact period its last condition is met. */
static void test_orders(void) {
    struct mute_gate g;
    set_level(0.0);

    /* lock, self-check, params */
    TEST_EQ(mute_gate_init(&g, RATE), 0);
    mute_gate_locked(&g);
    TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_SELFCHECK | MUTE_WAIT_PARAMS);
    TEST_EQ(feed(&g, P48, WIN48), 0u);
    TEST_EQ(g.state, MUTE_MUTED);
    TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_PARAMS);
    mute_gate_params_seen(&g);
    TEST_EQ(feed(&g, P48, 1), 1u);
    TEST_EQ(g.state, MUTE_FADING_IN);
    TEST_EQ(mute_gate_reason(&g), 0u);

    /* lock, params, self-check */
    TEST_EQ(mute_gate_init(&g, RATE), 0);
    mute_gate_locked(&g);
    TEST_EQ(feed(&g, P48, 200), 0u);
    mute_gate_params_seen(&g);
    TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_SELFCHECK);
    TEST_EQ(feed(&g, P48, WIN48), WIN48 - 200);

    /* params, lock, self-check; periods before the lock don't count */
    TEST_EQ(mute_gate_init(&g, RATE), 0);
    mute_gate_params_seen(&g);
    mute_gate_params_seen(&g);
    TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_LOCK | MUTE_WAIT_SELFCHECK);
    TEST_EQ(feed(&g, P48, 3 * WIN48), 0u);
    TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_LOCK | MUTE_WAIT_SELFCHECK);
    mute_gate_locked(&g);
    TEST_EQ(feed(&g, P48, WIN48), WIN48);
}

/* Muted output is exact +0.0 whatever goes in. */
static void test_muted_is_zero(void) {
    struct mute_gate g;
    double buf[P48];
    TEST_EQ(mute_gate_init(&g, RATE), 0);
    for (size_t i = 0; i < P48; i++)
        buf[i] = i % 2 ? -0.7 : 0.7;
    TEST_EQ(mute_gate_check(&g, buf, P48), 0);
    mute_gate_apply(&g, buf, P48);
    for (size_t i = 0; i < P48; i++)
        TEST_CHECK(buf[i] == 0.0 && !signbit(buf[i]));
}

/* Records the fade gain curve for period size n, over `frames` frames. */
static void fade_curve(size_t n, double *gain, size_t frames) {
    struct mute_gate g;
    double zeros[96] = {0};
    TEST_EQ(mute_gate_init(&g, RATE), 0);
    mute_gate_locked(&g);
    mute_gate_params_seen(&g);
    set_level(0.0);
    size_t win = 24000 / n;
    TEST_EQ(feed(&g, n, win - 1), 0u);

    /* The last window period, checked on zeros but applied to ones, so the
     * fade's first samples are visible. */
    TEST_EQ(mute_gate_check(&g, zeros, n), 0);
    TEST_EQ(g.state, MUTE_FADING_IN);
    for (size_t off = 0; off < frames; off += n) {
        for (size_t i = 0; i < n; i++)
            gain[off + i] = 1.0;
        if (off > 0)
            TEST_EQ(mute_gate_check(&g, gain + off, n), 0);
        mute_gate_apply(&g, gain + off, n);
        if (off + n < FADE)
            TEST_EQ(g.state, MUTE_FADING_IN);
        else
            TEST_EQ(g.state, MUTE_LIVE);
    }
}

static void test_fade(void) {
    static double g48[2400], g96[2400];
    fade_curve(48, g48, 2400);
    fade_curve(96, g96, 2400);

    for (size_t i = 0; i < 2400; i++)
        TEST_EQ(g48[i], i < FADE ? (double)(i + 1) / FADE : 1.0);
    TEST_EQ(g48[FADE - 1], 1.0);
    for (size_t i = 1; i < 2400; i++)
        TEST_CHECK(g48[i] >= g48[i - 1]);
    TEST_CHECK(memcmp(g48, g96, sizeof(g48)) == 0);
}

/* An xrun in FADING_IN or LIVE re-mutes and needs a new lock and window,
 * but not new params. */
static void test_xrun(void) {
    struct mute_gate g;
    for (int live = 0; live < 2; live++) {
        make_live(&g);
        if (!live) {
            mute_gate_unlocked(&g);
            mute_gate_locked(&g);
            TEST_EQ(feed(&g, P48, WIN48), WIN48);
            TEST_EQ(g.state, MUTE_FADING_IN);
        }
        mute_gate_unlocked(&g);
        TEST_EQ(g.state, MUTE_MUTED);
        TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_LOCK | MUTE_WAIT_SELFCHECK);
        TEST_EQ(feed(&g, P48, 100), 0u); /* the relock's settle: not counted */
        mute_gate_locked(&g);
        TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_SELFCHECK);
        TEST_EQ(feed(&g, P48, WIN48), WIN48);
    }
}

/* A non-finite sample while LIVE re-mutes in that same period. */
static void test_non_finite_live(void) {
    static const double bad[3] = {NAN, INFINITY, -INFINITY};
    struct mute_gate g;
    double buf[P48];
    for (size_t v = 0; v < 3; v++) {
        make_live(&g);
        for (size_t i = 0; i < P48; i++)
            buf[i] = 0.25;
        buf[10] = bad[v];
        TEST_EQ(mute_gate_check(&g, buf, P48), 1);
        TEST_EQ(g.state, MUTE_MUTED);
        TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_SELFCHECK);
        mute_gate_apply(&g, buf, P48);
        for (size_t i = 0; i < P48; i++)
            TEST_CHECK(buf[i] == 0.0 && !signbit(buf[i]));
        set_level(0.0);
        TEST_EQ(feed(&g, P48, WIN48), WIN48);
    }
}

/* A NaN halfway through a window restarts it. */
static void test_non_finite_mid_window(void) {
    struct mute_gate g;
    double buf[P48];
    TEST_EQ(mute_gate_init(&g, RATE), 0);
    mute_gate_locked(&g);
    mute_gate_params_seen(&g);
    set_level(0.0);
    TEST_EQ(feed(&g, P48, 250), 0u);
    memset(buf, 0, sizeof(buf));
    buf[0] = NAN;
    TEST_EQ(mute_gate_check(&g, buf, P48), 1);
    TEST_EQ(feed(&g, P48, WIN48 - 1), 0u);
    TEST_EQ(feed(&g, P48, 1), 1u);
}

static void test_dc(void) {
    static const struct { double level; int unmutes; } cases[] = {
        {0.02, 0}, {-0.02, 0}, {0.0101, 0}, {0.0099, 1}, {-0.0099, 1}, {0.005, 1},
    };
    struct mute_gate g;
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        TEST_EQ(mute_gate_init(&g, RATE), 0);
        mute_gate_locked(&g);
        mute_gate_params_seen(&g);
        set_level(cases[c].level);
        TEST_EQ(feed(&g, P48, 10 * WIN48), cases[c].unmutes ? WIN48 : 0u);
        if (!cases[c].unmutes)
            TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_SELFCHECK);
    }
}

/* The decided rule (whole-window mean, DC_BOUND 0.01) applied to sub-bass.
 * A whole number of cycles per window averages out. A steady 22 Hz at
 * -6 dBFS has 5.5 cycles per window, so every window's mean is about
 * +-0.029 and the output stays muted while the tone lasts. That is
 * fail-safe, and pinned here on purpose. */
static void test_sub_bass(void) {
    struct mute_gate g;
    TEST_EQ(mute_gate_init(&g, RATE), 0);
    mute_gate_locked(&g);
    mute_gate_params_seen(&g);
    set_sine(20.0, 0.5);
    TEST_EQ(feed(&g, P48, WIN48), WIN48);

    TEST_EQ(mute_gate_init(&g, RATE), 0);
    mute_gate_locked(&g);
    mute_gate_params_seen(&g);
    set_sine(22.0, 0.5);
    TEST_EQ(feed(&g, P48, 10 * WIN48), 0u);
}

/* While ctld is absent, each window is re-judged: a later window with DC
 * clears an earlier pass, so the unmute rests on the most recent window. */
static void test_recheck_until_params(void) {
    struct mute_gate g;
    TEST_EQ(mute_gate_init(&g, RATE), 0);
    mute_gate_locked(&g);

    set_level(0.0);
    TEST_EQ(feed(&g, P48, WIN48), 0u);
    TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_PARAMS);

    set_level(0.02);
    TEST_EQ(feed(&g, P48, WIN48), 0u);
    TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_SELFCHECK | MUTE_WAIT_PARAMS);

    /* ctld arrives, but the DC is still there: still muted. */
    mute_gate_params_seen(&g);
    TEST_EQ(feed(&g, P48, WIN48), 0u);
    TEST_EQ(mute_gate_reason(&g), MUTE_WAIT_SELFCHECK);

    set_level(0.0);
    TEST_EQ(feed(&g, P48, WIN48), WIN48);
}

int main(void) {
    test_init();
    test_orders();
    test_muted_is_zero();
    test_fade();
    test_xrun();
    test_non_finite_live();
    test_non_finite_mid_window();
    test_dc();
    test_sub_bass();
    test_recheck_until_params();
    return test_failures();
}
