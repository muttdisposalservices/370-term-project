/* os/rt-busycheck.c
 * F14 evidence gatherer: busy-spins on CLOCK_MONOTONIC with no syscalls in
 * the hot path (unlike cyclictest, which sleeps between samples and so
 * can't see RT-throttling or fair-server preemption of a thread that never
 * sleeps). Run pinned to audiod's RT thread's own core/policy/priority:
 *
 *   cc -std=c17 -O2 -Wall -Wextra -Werror -o rt-busycheck os/rt-busycheck.c
 *   sudo chrt -f 80 taskset -c 3 ./rt-busycheck [duration_s] [threshold_us]
 *
 * duration_s defaults to 180, threshold_us to 100. Any gap between
 * consecutive clock reads larger than threshold_us is logged with the
 * elapsed time and gap size; a summary prints at exit. Standalone -- not
 * built by the Makefile, same as rt-tune.sh.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Set by on_sigint(), polled each loop iteration so ctrl-C breaks the loop
 * and reaches the summary/flush instead of killing the process under it. */
static volatile sig_atomic_t g_stop;

static void on_sigint(int signum)
{
    (void)signum;
    g_stop = 1;
}

static int64_t ns_diff(struct timespec a, struct timespec b)
{
    return (int64_t)(b.tv_sec - a.tv_sec) * 1000000000LL +
           (int64_t)(b.tv_nsec - a.tv_nsec);
}

/* Accepts only a non-empty string of decimal digits (no sign, no
 * whitespace, no trailing junk). Returns 0 and sets *out on success. */
static int parse_positive_long(const char *s, long *out)
{
    if (s == NULL || s[0] == '\0') {
        return -1;
    }
    for (const char *p = s; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return -1;
        }
    }

    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0' || v <= 0) {
        return -1;
    }

    *out = v;
    return 0;
}

int main(int argc, char **argv)
{
    long duration_s = 180;
    long threshold_us = 100;

    if (argc > 1 && parse_positive_long(argv[1], &duration_s) != 0) {
        fprintf(stderr,
                "rt-busycheck: duration_s must be a positive integer, got '%s'\n",
                argv[1]);
        return 1;
    }
    if (argc > 2 && parse_positive_long(argv[2], &threshold_us) != 0) {
        fprintf(stderr,
                "rt-busycheck: threshold_us must be a positive integer, got '%s'\n",
                argv[2]);
        return 1;
    }

    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction");
        return 1;
    }

    struct timespec start;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime");
        return 1;
    }

    struct timespec prev = start;
    int64_t threshold_ns = threshold_us * 1000;
    uint64_t gap_count = 0;
    int64_t max_gap_ns = 0;
    uint64_t samples = 0;

    fprintf(stderr,
            "rt-busycheck: running %ld s, logging gaps over %ld us "
            "(ctrl-C to stop early)\n",
            duration_s, threshold_us);

    for (;;) {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            perror("clock_gettime");
            return 1;
        }
        samples++;

        int64_t gap = ns_diff(prev, now);
        if (gap > threshold_ns) {
            int64_t elapsed_ms = ns_diff(start, now) / 1000000;
            printf("t=%" PRId64 "ms gap=%" PRId64 "us\n", elapsed_ms,
                   gap / 1000);
            gap_count++;
            if (gap > max_gap_ns) {
                max_gap_ns = gap;
            }
        }
        prev = now;

        if (g_stop || ns_diff(start, now) >= (int64_t)duration_s * 1000000000LL) {
            break;
        }
    }

    fprintf(stderr,
            "rt-busycheck: done. %" PRIu64 " samples, %" PRIu64
            " gaps over %ld us, max gap %" PRId64 " us\n",
            samples, gap_count, threshold_us, max_gap_ns / 1000);

    return 0;
}
