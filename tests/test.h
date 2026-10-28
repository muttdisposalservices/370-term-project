/* Shared assert macros for the project's dependency-free test runner.
 * Each tests/test_*.c is a standalone program: include this, write checks,
 * return test_failures() from main(). A non-zero exit fails `make test`. */
#ifndef TEST_H
#define TEST_H

#include <math.h>
#include <stdio.h>

static int g_test_failures = 0;

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_test_failures++; \
        } \
    } while (0)

#define TEST_EQ(a, b) TEST_CHECK((a) == (b))

#define TEST_NEAR(a, b, eps) TEST_CHECK(fabs((double)(a) - (double)(b)) <= (eps))

static inline int test_failures(void) {
    return g_test_failures != 0;
}

#endif
