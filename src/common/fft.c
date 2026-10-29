#include "fft.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FFT_PI 3.14159265358979323846

int fft_init(struct fft *f, size_t n) {
    if (!f)
        return -EINVAL;
    memset(f, 0, sizeof(*f));
    if (n < 4 || (n & (n - 1)) != 0 || n / 2 > SIZE_MAX / sizeof(double complex))
        return -EINVAL;

    size_t m = n / 2;
    unsigned bits = 0;
    while (((size_t)1 << bits) < m)
        bits++;

    double complex *tw = malloc(m * sizeof(*tw));
    if (!tw)
        goto fail;
    size_t *rev = malloc(m * sizeof(*rev));
    if (!rev)
        goto fail_tw;
    double complex *scratch = malloc(m * sizeof(*scratch));
    if (!scratch)
        goto fail_rev;

    /* Each twiddle computed directly, so no error builds up across k. */
    for (size_t k = 0; k < m; k++) {
        double a = 2.0 * FFT_PI * (double)k / (double)n;
        tw[k] = CMPLX(cos(a), -sin(a));
    }
    for (size_t k = 0; k < m; k++) {
        size_t r = 0;
        for (unsigned b = 0; b < bits; b++)
            r |= ((k >> b) & 1u) << (bits - 1 - b);
        rev[k] = r;
    }

    f->n = n;
    f->tw = tw;
    f->rev = rev;
    f->scratch = scratch;
    return 0;

fail_rev:
    free(rev);
fail_tw:
    free(tw);
fail:
    return -ENOMEM;
}

void fft_free(struct fft *f) {
    if (!f)
        return;
    free(f->tw);
    free(f->rev);
    free(f->scratch);
    memset(f, 0, sizeof(*f));
}

/* In-place n/2-point complex FFT of a. inverse conjugates the twiddles;
 * neither direction scales. The size-m twiddle exp(-2*pi*i*j/m) is
 * tw[2*j]. */
static void cfft(const struct fft *f, double complex *a, int inverse) {
    size_t m = f->n / 2;

    for (size_t k = 0; k < m; k++) {
        size_t r = f->rev[k];
        if (k < r) {
            double complex t = a[k];
            a[k] = a[r];
            a[r] = t;
        }
    }

    for (size_t len = 2; len <= m; len *= 2) {
        size_t half = len / 2;
        size_t step = 2 * (m / len);
        for (size_t s = 0; s < m; s += len) {
            for (size_t j = 0; j < half; j++) {
                double complex w = f->tw[j * step];
                if (inverse)
                    w = conj(w);
                double complex t = w * a[s + j + half];
                a[s + j + half] = a[s + j] - t;
                a[s + j] += t;
            }
        }
    }
}

void fft_forward(struct fft *f, const double *in, double complex *out) {
    size_t m = f->n / 2;
    double complex *z = f->scratch;

    /* Even samples as the real part, odd as the imaginary part. */
    for (size_t k = 0; k < m; k++)
        z[k] = CMPLX(in[2 * k], in[2 * k + 1]);
    cfft(f, z, 0);

    /* X[k] = E[k] + W^k O[k], with E and O (the even and odd halves'
     * spectra) recovered from Z[k] and conj(Z[m-k]). */
    out[0] = creal(z[0]) + cimag(z[0]);
    out[m] = creal(z[0]) - cimag(z[0]);
    for (size_t k = 1; k < m; k++) {
        double complex zc = conj(z[m - k]);
        double complex e = 0.5 * (z[k] + zc);
        double complex o = -0.5 * I * (z[k] - zc);
        out[k] = e + f->tw[k] * o;
    }
}

void fft_inverse(struct fft *f, const double complex *in, double *out) {
    size_t m = f->n / 2;
    double complex *z = f->scratch;

    /* Undo the split: E = (X[k] + conj(X[m-k]))/2,
     * O = (X[k] - conj(X[m-k])) conj(W^k)/2, Z = E + iO. */
    for (size_t k = 0; k < m; k++) {
        double complex xc = conj(in[m - k]);
        double complex e = 0.5 * (in[k] + xc);
        double complex o = 0.5 * (in[k] - xc) * conj(f->tw[k]);
        z[k] = e + I * o;
    }
    /* Bin 0 and bin m are real for real input; use only their real parts. */
    z[0] = CMPLX(0.5 * (creal(in[0]) + creal(in[m])),
                 0.5 * (creal(in[0]) - creal(in[m])));

    cfft(f, z, 1);

    double scale = 1.0 / (double)m;
    for (size_t k = 0; k < m; k++) {
        out[2 * k] = creal(z[k]) * scale;
        out[2 * k + 1] = cimag(z[k]) * scale;
    }
}
