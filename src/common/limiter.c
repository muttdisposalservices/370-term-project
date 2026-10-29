#include "limiter.h"

#include <errno.h>
#include <math.h>

int limiter_ceiling_valid(double dbfs) {
    return isfinite(dbfs) && dbfs <= 0.0;
}

int limiter_init(struct limiter *l, double ceiling_dbfs, uint32_t rate) {
    if (!l || !limiter_ceiling_valid(ceiling_dbfs) || rate == 0)
        return -EINVAL;
    l->ceiling = pow(10.0, ceiling_dbfs / 20.0);
    l->gain = 1.0;
    /* 1 - exp(-1/(tau*rate)), via expm1 so the small result keeps its digits. */
    l->release_k = -expm1(-1.0 / (LIMITER_RELEASE_MS / 1000.0 * (double)rate));
    return 0;
}

int limiter_set_ceiling(struct limiter *l, double dbfs) {
    if (!l || !limiter_ceiling_valid(dbfs))
        return -EINVAL;
    l->ceiling = pow(10.0, dbfs / 20.0);
    return 0;
}

double limiter_process(struct limiter *l, double *buf, size_t n) {
    double c = l->ceiling;
    double g = l->gain;
    double k = l->release_k;
    double gmin = n ? 1.0 : g;

    for (size_t i = 0; i < n; i++) {
        double x = buf[i];
        double a = fabs(x);
        double target = a > c ? c / a : 1.0;
        if (target < g)
            g = target;
        else
            g += (target - g) * k;
        if (g < gmin)
            gmin = g;

        /* Absorbs rounding in x * (c / a). Plain comparisons, not fmin/fmax:
         * those would turn a NaN into c and hide it from the mute gate. */
        double y = x * g;
        if (y > c)
            y = c;
        else if (y < -c)
            y = -c;
        buf[i] = y;
    }
    l->gain = g;
    return gmin;
}
