/* EQ biquad cascade (src/common/biquad.c, plan.md C11). */
#include "test.h"
#include "biquad.h"
#include "fft.h"

#include <complex.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FS     96000.0
#define Q3     4.318473 /* 1/3-octave bandwidth */
#define BIG_N  32768u
#define PI     3.14159265358979323846

static uint64_t g_rng = 0x9e3779b97f4a7c15u;

/* Deterministic uniform value in [-1, 1). */
static double rnd(void) {
    g_rng = g_rng * 6364136223846793005u + 1442695040888963407u;
    return (double)(g_rng >> 11) / (double)(1u << 31) / (double)(1u << 22) * 2.0 - 1.0;
}

/* RBJ cookbook peaking EQ, normalised to a0 = 1. Its gain is 10^(db/20) at
 * f0 and exactly 1 at DC and Nyquist. */
static struct biquad_coef peaking(double f0, double q, double db) {
    double A = pow(10.0, db / 40.0);
    double w0 = 2.0 * PI * f0 / FS;
    double alpha = sin(w0) / (2.0 * q);
    double a0 = 1.0 + alpha / A;
    struct biquad_coef k = {
        (1.0 + alpha * A) / a0, -2.0 * cos(w0) / a0, (1.0 - alpha * A) / a0,
        -2.0 * cos(w0) / a0, (1.0 - alpha / A) / a0,
    };
    return k;
}

static void test_flat(void) {
    struct eq_coefs c;
    struct eq_state s;
    double x[512], y[512];

    eq_flat(&c);
    TEST_EQ(eq_coefs_valid(&c), 1);
    eq_reset(&s);

    for (size_t i = 0; i < 512; i++)
        x[i] = rnd();
    x[0] = 1.0;
    x[1] = -1.0;
    memcpy(y, x, sizeof(x));
    eq_process(&c, &s, y, 300);
    eq_process(&c, &s, y + 300, 212);
    TEST_CHECK(memcmp(x, y, sizeof(x)) == 0);
}

static void test_valid(void) {
    struct eq_coefs c;

    TEST_EQ(eq_coefs_valid(NULL), 0);

    memset(&c, 0, sizeof(c));
    TEST_EQ(eq_coefs_valid(&c), 0); /* a zeroed tbuf slot */

    /* Real 1/3-octave bands at both ends of the range, boost and cut. The
     * 20 Hz one has poles at r ~ 0.9993. */
    eq_flat(&c);
    c.band[0] = peaking(20.0, Q3, 12.0);
    c.band[1] = peaking(20.0, Q3, -12.0);
    c.band[17] = peaking(20000.0, Q3, 12.0);
    c.band[18] = peaking(20000.0, Q3, -12.0);
    TEST_EQ(eq_coefs_valid(&c), 1);

    /* A non-finite value in each position of one band. */
    static const double bad[] = {NAN, INFINITY, -INFINITY};
    for (size_t v = 0; v < 3; v++) {
        for (size_t p = 0; p < 5; p++) {
            eq_flat(&c);
            struct biquad_coef *k = &c.band[7];
            double *field[5] = {&k->b0, &k->b1, &k->b2, &k->a1, &k->a2};
            *field[p] = bad[v];
            TEST_EQ(eq_coefs_valid(&c), 0);
        }
    }

    /* Stability triangle, strict at every edge. */
    struct { double a1, a2; int ok; } tri[] = {
        {0.0, 0.999, 1},  {0.0, 1.0, 0},   {0.0, -1.0, 0},   {0.0, -0.999, 1},
        {1.5, 0.5, 0},    {-1.5, 0.5, 0},  {1.499, 0.5, 1},  {-1.499, 0.5, 1},
        {2.0, 1.0, 0},    {0.1, -0.9, 0},  {0.09, -0.9, 1},  {0.0, 0.0, 1},
    };
    for (size_t i = 0; i < sizeof(tri) / sizeof(tri[0]); i++) {
        eq_flat(&c);
        c.band[4].a1 = tri[i].a1;
        c.band[4].a2 = tri[i].a2;
        TEST_EQ(eq_coefs_valid(&c), tri[i].ok);
    }

    /* One band with an all-zero numerator; any nonzero b keeps it valid. */
    eq_flat(&c);
    c.band[12].b0 = 0.0;
    TEST_EQ(eq_coefs_valid(&c), 0);
    c.band[12].b2 = 1e-9;
    TEST_EQ(eq_coefs_valid(&c), 1);
}

/* |H| from the FFT of the cascade's impulse response at the bin nearest f0. */
static void check_gain(const struct eq_coefs *c, size_t bin, double want_f0) {
    struct fft f;
    struct eq_state s;
    double *h = calloc(BIG_N, sizeof(*h));
    double complex *H = malloc((BIG_N / 2 + 1) * sizeof(*H));
    TEST_CHECK(h && H);
    if (!h || !H)
        goto out;
    TEST_EQ(fft_init(&f, BIG_N), 0);

    h[0] = 1.0;
    eq_reset(&s);
    eq_process(c, &s, h, BIG_N);
    fft_forward(&f, h, H);

    TEST_NEAR(cabs(H[bin]), want_f0, 1e-9);
    TEST_NEAR(cabs(H[0]), 1.0, 1e-9);
    TEST_NEAR(cabs(H[BIG_N / 2]), 1.0, 1e-9);
    fft_free(&f);
out:
    free(h);
    free(H);
}

static void test_peaking_gain(void) {
    const size_t bin = 341; /* f0 = 341 * fs / BIG_N ~ 999 Hz, exactly on a bin */
    double f0 = (double)bin * FS / BIG_N;
    struct eq_coefs c;

    eq_flat(&c);
    c.band[9] = peaking(f0, Q3, 6.0);
    check_gain(&c, bin, pow(10.0, 6.0 / 20.0));

    /* Two bands in the cascade multiply. */
    c.band[10] = peaking(f0, Q3, -9.0);
    check_gain(&c, bin, pow(10.0, -3.0 / 20.0));
}

/* With from == to, the ramp is bit-exact with eq_process. */
static void test_ramp_same(void) {
    struct eq_coefs c;
    struct eq_state s1, s2;
    double a[480], b[480];

    eq_flat(&c);
    c.band[3] = peaking(100.0, Q3, 6.0);
    c.band[11] = peaking(4000.0, Q3, -4.0);
    for (size_t i = 0; i < 480; i++)
        a[i] = b[i] = rnd();
    eq_reset(&s1);
    eq_reset(&s2);
    for (size_t p = 0; p < 10; p++) {
        eq_process(&c, &s1, a + 48 * p, 48);
        eq_process_ramp(&c, &c, &s2, b + 48 * p, 48);
    }
    TEST_CHECK(memcmp(a, b, sizeof(a)) == 0);
    TEST_CHECK(memcmp(&s1, &s2, sizeof(s1)) == 0);
}

/* A pure-gain band ramped from 1 to 2: sample i gets 1 + (i+1)/n, and the
 * last sample is exactly 2. */
static void test_ramp_shape(void) {
    struct eq_coefs from, to;
    struct eq_state s;
    double y[48];

    eq_flat(&from);
    eq_flat(&to);
    to.band[5].b0 = 2.0;
    eq_reset(&s);
    for (size_t i = 0; i < 48; i++)
        y[i] = 1.0;
    eq_process_ramp(&from, &to, &s, y, 48);
    for (size_t i = 0; i < 48; i++)
        TEST_EQ(y[i], 1.0 + (double)(i + 1) / 48.0);
    TEST_EQ(y[47], 2.0);

    /* n = 1 uses `to` alone; n = 0 touches nothing. */
    y[0] = 1.0;
    eq_process_ramp(&from, &to, &s, y, 1);
    TEST_EQ(y[0], 2.0);
    struct eq_state before = s;
    y[0] = 7.0;
    eq_process_ramp(&from, &to, &s, y, 0);
    TEST_EQ(y[0], 7.0);
    TEST_CHECK(memcmp(&before, &s, sizeof(s)) == 0);
}

/* Ramping between two real EQs, then running on `to`, converges to `to`'s
 * own output once the old state has decayed. */
static void test_ramp_handoff(void) {
    struct eq_coefs from, to;
    struct eq_state s, ref;
    double *x = malloc(BIG_N * sizeof(*x));
    double *y = malloc(BIG_N * sizeof(*y));
    double *r = malloc(BIG_N * sizeof(*r));
    TEST_CHECK(x && y && r);
    if (!x || !y || !r)
        goto out;

    eq_flat(&from);
    eq_flat(&to);
    /* Mid-band, so the old state decays below 1e-9 within the test length
     * (a 20 Hz band's poles at r ~ 0.9999 would need far longer). */
    from.band[6] = peaking(250.0, Q3, -12.0);
    to.band[6] = peaking(315.0, Q3, 12.0);
    from.band[14] = peaking(2500.0, Q3, 9.0);
    to.band[14] = peaking(2500.0, Q3, -9.0);
    TEST_EQ(eq_coefs_valid(&from), 1);
    TEST_EQ(eq_coefs_valid(&to), 1);

    for (size_t i = 0; i < BIG_N; i++)
        x[i] = r[i] = rnd();
    memcpy(y, x, BIG_N * sizeof(*x));

    eq_reset(&s);
    eq_reset(&ref);
    eq_process(&from, &s, y, 4800);
    eq_process_ramp(&from, &to, &s, y + 4800, 48);
    eq_process(&to, &s, y + 4848, BIG_N - 4848);
    eq_process(&to, &ref, r, BIG_N);

    double worst = 0.0;
    for (size_t i = BIG_N - 1000; i < BIG_N; i++)
        if (fabs(y[i] - r[i]) > worst)
            worst = fabs(y[i] - r[i]);
    TEST_CHECK(worst < 1e-9);
out:
    free(x);
    free(y);
    free(r);
}

/* A NaN recirculates through y1/y2 for good; eq_reset clears it. */
static void test_reset_clears_nan(void) {
    struct eq_coefs c;
    struct eq_state s;
    double y[200] = {0};

    eq_flat(&c);
    eq_reset(&s);
    y[0] = NAN;
    eq_process(&c, &s, y, 200);
    TEST_CHECK(isnan(y[199]));

    eq_reset(&s);
    memset(y, 0, sizeof(y));
    eq_process(&c, &s, y, 200);
    for (size_t i = 0; i < 200; i++)
        TEST_EQ(y[i], 0.0);
}

int main(void) {
    test_flat();
    test_valid();
    test_peaking_gain();
    test_ramp_same();
    test_ramp_shape();
    test_ramp_handoff();
    test_reset_clears_nan();
    return test_failures();
}
