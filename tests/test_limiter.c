/* Output limiter (src/common/limiter.c, plan.md C11). */
#include "test.h"
#include "limiter.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#define RATE    96000u
#define BLOCK   4800u /* LIMITER_RELEASE_MS at RATE: one time constant */

static uint64_t g_rng = 0x9e3779b97f4a7c15u;

/* Deterministic uniform value in [-1, 1). */
static double rnd(void) {
    g_rng = g_rng * 6364136223846793005u + 1442695040888963407u;
    return (double)(g_rng >> 11) / (double)(1u << 31) / (double)(1u << 22) * 2.0 - 1.0;
}

static void test_ceiling_rules(void) {
    struct limiter l;

    TEST_EQ(limiter_ceiling_valid(NAN), 0);
    TEST_EQ(limiter_ceiling_valid(INFINITY), 0);
    TEST_EQ(limiter_ceiling_valid(-INFINITY), 0);
    TEST_EQ(limiter_ceiling_valid(0.1), 0);
    TEST_EQ(limiter_ceiling_valid(0.0), 1);
    TEST_EQ(limiter_ceiling_valid(-0.0), 1);
    TEST_EQ(limiter_ceiling_valid(-6.0), 1);
    TEST_EQ(limiter_ceiling_valid(-200.0), 1);

    TEST_EQ(limiter_init(NULL, -6.0, RATE), -EINVAL);
    TEST_EQ(limiter_init(&l, NAN, RATE), -EINVAL);
    TEST_EQ(limiter_init(&l, 0.1, RATE), -EINVAL);
    TEST_EQ(limiter_init(&l, -6.0, 0), -EINVAL);

    TEST_EQ(limiter_init(&l, LIMITER_DEFAULT_CEILING_DBFS, RATE), 0);
    TEST_NEAR(l.ceiling, pow(10.0, -0.3), 1e-15);
    TEST_EQ(l.gain, 1.0);
    TEST_EQ(limiter_init(&l, 0.0, RATE), 0);
    TEST_EQ(l.ceiling, 1.0);
}

/* Below the ceiling nothing changes, bit for bit. */
static void test_transparent(void) {
    struct limiter l;
    static double x[BLOCK], y[BLOCK];

    TEST_EQ(limiter_init(&l, -3.0, RATE), 0);
    for (size_t i = 0; i < BLOCK; i++)
        x[i] = 0.5 * rnd();
    memcpy(y, x, sizeof(x));
    TEST_EQ(limiter_process(&l, y, BLOCK), 1.0);
    TEST_CHECK(memcmp(x, y, sizeof(x)) == 0);
    TEST_EQ(l.gain, 1.0);

    /* n = 0 returns the current gain and touches nothing. */
    TEST_EQ(limiter_process(&l, y, 0), 1.0);
}

/* +20 dB random input never gets past the ceiling, compared exactly. */
static void test_brickwall(void) {
    struct limiter l;
    static double y[BLOCK];

    TEST_EQ(limiter_init(&l, -6.0, RATE), 0);
    for (size_t p = 0; p < 20; p++) {
        for (size_t i = 0; i < BLOCK; i++)
            y[i] = 10.0 * rnd();
        limiter_process(&l, y, BLOCK);
        for (size_t i = 0; i < BLOCK; i++)
            TEST_CHECK(fabs(y[i]) <= l.ceiling);
    }
    TEST_CHECK(l.gain >= 0.0 && l.gain <= 1.0);

    /* x * (c / x) rounds one ulp above c for about 1 input in 10^6, too
     * rarely for the random blocks to hit. This is one (found by search),
     * so the rounding guard is exercised every run. */
    TEST_EQ(limiter_init(&l, -6.0, RATE), 0);
    double x = 2.0042593502442188;
    TEST_CHECK(x * (l.ceiling / x) > l.ceiling);
    limiter_process(&l, &x, 1);
    TEST_EQ(x, l.ceiling);
}

/* A step over the ceiling is on the ceiling at its first sample, then the
 * gain recovers with a 50 ms time constant. */
static void test_attack_and_release(void) {
    struct limiter l;
    static double y[3 * BLOCK];

    TEST_EQ(limiter_init(&l, -6.0, RATE), 0);
    double c = l.ceiling;

    double z[4] = {0.0, 0.0, 0.0, 1.0};
    TEST_EQ(limiter_process(&l, z, 4), c);
    TEST_EQ(z[3], c);
    TEST_EQ(l.gain, c);

    /* Input drops below the ceiling; the recovered share of the gap is
     * 1 - exp(-n / tau). */
    for (size_t i = 0; i < 3 * BLOCK; i++)
        y[i] = 0.01;
    limiter_process(&l, y, BLOCK);
    TEST_NEAR((l.gain - c) / (1.0 - c), 1.0 - exp(-1.0), 1e-9);
    limiter_process(&l, y + BLOCK, 2 * BLOCK);
    TEST_NEAR((l.gain - c) / (1.0 - c), 1.0 - exp(-3.0), 1e-9);

    /* The returned minimum is the lowest gain applied in that block. */
    double g0 = l.gain;
    double one = 0.01;
    TEST_EQ(limiter_process(&l, &one, 1), l.gain);
    TEST_CHECK(l.gain > g0);
}

/* A NaN comes out as NaN at the same index; Inf comes out as NaN and drops
 * the gain to 0. Either way gain stays finite and recovers. */
static void test_non_finite(void) {
    struct limiter l;
    static double y[BLOCK];

    TEST_EQ(limiter_init(&l, -6.0, RATE), 0);
    double a[3] = {0.1, NAN, 0.1};
    limiter_process(&l, a, 3);
    TEST_EQ(a[0], 0.1);
    TEST_CHECK(isnan(a[1]));
    TEST_EQ(a[2], 0.1);
    TEST_EQ(l.gain, 1.0);

    static const double infs[2] = {INFINITY, -INFINITY};
    for (size_t v = 0; v < 2; v++) {
        TEST_EQ(limiter_init(&l, -6.0, RATE), 0);
        double b[3] = {0.1, infs[v], 0.1};
        TEST_EQ(limiter_process(&l, b, 3), 0.0);
        TEST_EQ(b[0], 0.1);
        TEST_CHECK(isnan(b[1]));
        TEST_CHECK(isfinite(b[2]) && fabs(b[2]) < 0.1);
        TEST_CHECK(isfinite(l.gain) && l.gain >= 0.0 && l.gain <= 1.0);

        for (size_t p = 0; p < 10; p++) {
            for (size_t i = 0; i < BLOCK; i++)
                y[i] = 0.1;
            limiter_process(&l, y, BLOCK);
        }
        TEST_CHECK(l.gain > 0.99 && l.gain <= 1.0);
    }
}

static void test_set_ceiling(void) {
    struct limiter l;

    TEST_EQ(limiter_init(&l, -6.0, RATE), 0);
    double y[2] = {0.4, 0.4};
    limiter_process(&l, y, 1);
    TEST_EQ(y[0], 0.4);

    /* Lowered: applies from the very next sample. */
    TEST_EQ(limiter_set_ceiling(&l, -12.0), 0);
    double c12 = pow(10.0, -12.0 / 20.0);
    TEST_NEAR(l.ceiling, c12, 1e-15);
    limiter_process(&l, y + 1, 1);
    TEST_CHECK(y[1] <= l.ceiling);
    TEST_NEAR(y[1], c12, 1e-15);

    /* Invalid: rejected, ceiling unchanged bit for bit. */
    double before = l.ceiling;
    TEST_EQ(limiter_set_ceiling(&l, NAN), -EINVAL);
    TEST_EQ(limiter_set_ceiling(&l, INFINITY), -EINVAL);
    TEST_EQ(limiter_set_ceiling(&l, 0.5), -EINVAL);
    TEST_EQ(limiter_set_ceiling(NULL, -6.0), -EINVAL);
    TEST_CHECK(memcmp(&before, &l.ceiling, sizeof(before)) == 0);
}

int main(void) {
    test_ceiling_rules();
    test_transparent();
    test_brickwall();
    test_attack_and_release();
    test_non_finite();
    test_set_ceiling();
    return test_failures();
}
