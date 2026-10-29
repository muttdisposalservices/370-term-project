/* Real-input FFT (src/common/fft.c, plan.md F5). */
#include "test.h"
#include "fft.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BIG_N  32768u
#define TWO_PI 6.28318530717958647692

static uint64_t g_rng = 0x9e3779b97f4a7c15u;

/* Deterministic uniform value in [-1, 1). */
static double rnd(void) {
    g_rng = g_rng * 6364136223846793005u + 1442695040888963407u;
    return (double)(g_rng >> 11) / (double)(1u << 31) / (double)(1u << 22) * 2.0 - 1.0;
}

static void test_init_rejects(void) {
    static const size_t bad[] = {0, 1, 2, 3, 6, 1000};
    struct fft f;
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_EQ(fft_init(&f, bad[i]), -EINVAL);
        fft_free(&f); /* zeroed on failure, so safe */
    }
    TEST_EQ(fft_init(NULL, 8), -EINVAL);

    TEST_EQ(fft_init(&f, 8), 0);
    fft_free(&f);
    fft_free(&f);
    fft_free(NULL);
}

/* Bins 0..n/2 of the naive O(n^2) DFT match fft_forward. */
static void test_vs_naive(size_t n) {
    struct fft f;
    double *x = malloc(n * sizeof(*x));
    double complex *got = malloc((n / 2 + 1) * sizeof(*got));
    TEST_CHECK(x && got);
    if (!x || !got || fft_init(&f, n) != 0) {
        TEST_CHECK(0);
        goto out;
    }

    for (size_t j = 0; j < n; j++)
        x[j] = rnd();
    fft_forward(&f, x, got);

    double worst = 0.0, scale = 0.0;
    for (size_t k = 0; k <= n / 2; k++) {
        double complex want = 0.0;
        for (size_t j = 0; j < n; j++) {
            double a = TWO_PI * (double)((k * j) % n) / (double)n;
            want += x[j] * CMPLX(cos(a), -sin(a));
        }
        double e = cabs(got[k] - want);
        if (e > worst)
            worst = e;
        if (cabs(want) > scale)
            scale = cabs(want);
    }
    TEST_CHECK(worst <= 1e-9 * scale);
    fft_free(&f);
out:
    free(x);
    free(got);
}

static void test_impulse_and_dc(void) {
    enum { N = 64 };
    struct fft f;
    double x[N];
    double complex X[N / 2 + 1];
    TEST_EQ(fft_init(&f, N), 0);

    memset(x, 0, sizeof(x));
    x[0] = 1.0;
    fft_forward(&f, x, X);
    for (size_t k = 0; k <= N / 2; k++)
        TEST_NEAR(cabs(X[k] - 1.0), 0.0, 1e-12);

    for (size_t j = 0; j < N; j++)
        x[j] = 0.5;
    fft_forward(&f, x, X);
    TEST_NEAR(creal(X[0]), 0.5 * N, 1e-12);
    TEST_NEAR(cimag(X[0]), 0.0, 1e-12);
    for (size_t k = 1; k <= N / 2; k++)
        TEST_NEAR(cabs(X[k]), 0.0, 1e-12);

    fft_free(&f);
}

/* A cosine at bin b puts n/2 in bin b and nothing elsewhere (n at b = n/2). */
static void test_single_bin(struct fft *f, double *x, double complex *X, size_t b) {
    size_t n = f->n;
    for (size_t j = 0; j < n; j++)
        x[j] = cos(TWO_PI * (double)((b * j) % n) / (double)n);
    fft_forward(f, x, X);

    double peak = (b == 0 || b == n / 2) ? (double)n : (double)n / 2.0;
    double leak = 0.0;
    for (size_t k = 0; k <= n / 2; k++)
        if (k != b && cabs(X[k]) > leak)
            leak = cabs(X[k]);
    TEST_NEAR(cabs(X[b]), peak, 1e-7);
    TEST_CHECK(leak <= 1e-7);
}

static void test_big(void) {
    struct fft f;
    double *x = malloc(BIG_N * sizeof(*x));
    double *y = malloc(BIG_N * sizeof(*y));
    double complex *X = malloc((BIG_N / 2 + 1) * sizeof(*X));
    TEST_CHECK(x && y && X);
    if (!x || !y || !X || fft_init(&f, BIG_N) != 0) {
        TEST_CHECK(0);
        goto out;
    }

    static const size_t bins[] = {0, 1, 2, 1000, BIG_N / 4, BIG_N / 2 - 1, BIG_N / 2};
    for (size_t i = 0; i < sizeof(bins) / sizeof(bins[0]); i++)
        test_single_bin(&f, x, X, bins[i]);

    for (size_t j = 0; j < BIG_N; j++)
        x[j] = rnd();
    fft_forward(&f, x, X);

    /* Parseval: sum x^2 == (1/n) sum over all n bins of |X|^2; bins 1..n/2-1
     * appear twice by symmetry. */
    double et = 0.0, ef = 0.0;
    for (size_t j = 0; j < BIG_N; j++)
        et += x[j] * x[j];
    for (size_t k = 0; k <= BIG_N / 2; k++) {
        double p = creal(X[k]) * creal(X[k]) + cimag(X[k]) * cimag(X[k]);
        ef += (k == 0 || k == BIG_N / 2) ? p : 2.0 * p;
    }
    ef /= BIG_N;
    TEST_NEAR(ef / et, 1.0, 1e-12);

    fft_inverse(&f, X, y);
    double worst = 0.0;
    for (size_t j = 0; j < BIG_N; j++)
        if (fabs(y[j] - x[j]) > worst)
            worst = fabs(y[j] - x[j]);
    TEST_CHECK(worst <= 1e-12);

    fft_free(&f);
out:
    free(x);
    free(y);
    free(X);
}

int main(void) {
    test_init_rejects();
    test_vs_naive(4);
    test_vs_naive(8);
    test_vs_naive(64);
    test_vs_naive(1024);
    test_impulse_and_dc();
    test_big();
    return test_failures();
}
