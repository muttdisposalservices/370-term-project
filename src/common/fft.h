/* Real-input FFT for analyzerd (plan.md F5). Our own code: an n-point real
 * transform runs as an n/2-point iterative radix-2 complex FFT plus the
 * standard even/odd split. n must be a power of two, at least 4.
 *
 * All tables and the work buffer are allocated once by fft_init, so the
 * transforms never allocate. One struct fft is for one thread at a time
 * (the transforms use its scratch buffer). */
#ifndef FFT_H
#define FFT_H

#include <complex.h>
#include <stddef.h>

struct fft {
    size_t n;                /* real length */
    double complex *tw;      /* tw[k] = exp(-2*pi*i*k/n), k < n/2 */
    size_t *rev;             /* bit reversal of k over log2(n/2) bits */
    double complex *scratch; /* n/2 work buffer */
};

/* Returns 0, -EINVAL (f NULL, n not a power of two, or n < 4) or -ENOMEM.
 * On failure *f is left zeroed, so fft_free is still safe. */
int fft_init(struct fft *f, size_t n);

/* Frees the tables and zeroes *f. Safe on a zeroed or already-freed struct. */
void fft_free(struct fft *f);

/* in: n reals. out: bins 0..n/2 (n/2+1 values); the rest follow by
 * conjugate symmetry. Unscaled. */
void fft_forward(struct fft *f, const double *in, double complex *out);

/* in: bins 0..n/2. out: n reals, scaled by 1/n, so
 * fft_inverse(fft_forward(x)) == x. The imaginary parts of bins 0 and n/2
 * are ignored. */
void fft_inverse(struct fft *f, const double complex *in, double *out);

#endif
