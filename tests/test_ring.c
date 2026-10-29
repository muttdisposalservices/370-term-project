/* Overwriting SPSC ring (src/common/ring.c, resolved_issues.md C9). */
#define _DEFAULT_SOURCE

#include "test.h"
#include "ring.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

/* Each frame carries its own index twice, so a torn or misplaced frame
 * shows up as a mismatch. */
struct frame {
    uint32_t i;
    uint32_t inv;
};

#define FS          ((uint32_t)sizeof(struct frame))
#define CAP         64u
#define STRESS_CAP  256u
#define STRESS_N    (1u << 20)
#define STRESS_PUSH 48u /* one period */
#define STRESS_READ 64u

static _Alignas(RING_ALIGN) unsigned char g_buf[RING_ALIGN + CAP * sizeof(struct frame)];

static void make(struct frame *f, uint64_t start, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) {
        f[k].i = (uint32_t)(start + k);
        f[k].inv = ~f[k].i;
    }
}

/* Returns 1 if f holds frames start..start+n-1, untorn. */
static int frames_are(const struct frame *f, uint64_t start, uint32_t n) {
    for (uint32_t k = 0; k < n; k++)
        if (f[k].i != (uint32_t)(start + k) || f[k].inv != ~f[k].i)
            return 0;
    return 1;
}

static void push(struct ring_writer *w, uint64_t start, uint32_t n) {
    struct frame f[CAP];
    make(f, start, n);
    TEST_EQ(ring_push(w, f, n), 0);
}

static struct ring *fresh(struct ring_writer *w, struct ring_reader *rd) {
    struct ring *r = (struct ring *)g_buf;
    memset(g_buf, 0, sizeof g_buf);
    TEST_EQ(ring_init(r, CAP, FS, 1, w), 0);
    TEST_EQ(ring_reader_attach(rd, r, CAP, FS), 0);
    return r;
}

static void test_args(void) {
    struct ring *r = (struct ring *)g_buf;
    struct ring *unaligned = (struct ring *)(g_buf + 8);
    struct ring_writer w;
    struct ring_reader rd;
    struct frame f[2];

    TEST_EQ(ring_bytes(CAP, FS), sizeof g_buf);
    TEST_EQ(ring_bytes(0, FS), 0);
    TEST_EQ(ring_bytes(48, FS), 0);
    TEST_EQ(ring_bytes(CAP, 0), 0);
    TEST_EQ(ring_init(unaligned, CAP, FS, 1, &w), -EINVAL);
    TEST_EQ(ring_init(r, 48, FS, 1, &w), -EINVAL);

    memset(g_buf, 0, sizeof g_buf);
    TEST_EQ(ring_reader_attach(&rd, r, CAP, FS), -EPROTO);
    TEST_EQ(ring_init(r, CAP, FS, 1, &w), 0);
    TEST_EQ(ring_reader_attach(&rd, unaligned, CAP, FS), -EINVAL);
    TEST_EQ(ring_reader_attach(&rd, r, CAP / 2, FS), -EPROTO);
    TEST_EQ(ring_reader_attach(&rd, r, CAP, FS + 4), -EPROTO);
    TEST_EQ(ring_reader_attach(&rd, r, CAP, FS), 0);

    TEST_EQ(ring_push(&w, f, CAP + 1), -EINVAL);
    TEST_EQ(ring_read(&rd, f, 0), -EINVAL);
    TEST_EQ(ring_read(&rd, f, CAP + 1), -EINVAL);
}

static void test_push_read(void) {
    struct ring_writer w;
    struct ring_reader rd;
    struct frame f[CAP];
    fresh(&w, &rd);

    TEST_EQ(ring_read(&rd, f, 1), -EAGAIN);
    push(&w, 0, 10);
    TEST_EQ(ring_read(&rd, f, 11), -EAGAIN);
    TEST_EQ(ring_read(&rd, f, 10), 0);
    TEST_CHECK(frames_are(f, 0, 10));

    /* Cross the wrap boundary several times at odd offsets. */
    uint64_t at = 10;
    for (; at < 5 * CAP; at += 25) {
        push(&w, at, 25);
        TEST_EQ(ring_read(&rd, f, 25), 0);
        TEST_CHECK(frames_are(f, at, 25));
    }

    /* A completely full ring is still readable. */
    push(&w, at, CAP);
    TEST_EQ(ring_read(&rd, f, CAP), 0);
    TEST_CHECK(frames_are(f, at, CAP));
    TEST_EQ(rd.dropped, 0);
}

static void test_lap(void) {
    struct ring_writer w;
    struct ring_reader rd;
    struct frame f[CAP];
    fresh(&w, &rd);

    push(&w, 0, 8);
    TEST_EQ(ring_read(&rd, f, 8), 0);

    push(&w, 8, CAP);
    push(&w, 8 + CAP, 5);
    TEST_EQ(ring_read(&rd, f, 4), -EPIPE);
    TEST_EQ(rd.dropped, CAP + 5);
    TEST_EQ(rd.pos, 8 + CAP + 5);
    TEST_EQ(ring_read(&rd, f, 1), -EAGAIN);

    push(&w, 8 + CAP + 5, 3);
    TEST_EQ(ring_read(&rd, f, 3), 0);
    TEST_CHECK(frames_are(f, 8 + CAP + 5, 3));
}

/* A writer caught mid-push: it has claimed frames CAP..CAP+3, overwriting
 * the slots of frames 0..3, but not committed them. A single post-write
 * counter would still say frames 0..3 are intact. */
static void test_inflight(void) {
    struct ring_writer w;
    struct ring_reader rd, rd2;
    struct frame f[CAP];
    struct ring *r = fresh(&w, &rd);
    TEST_EQ(ring_reader_attach(&rd2, r, CAP, FS), 0);

    push(&w, 0, CAP);
    atomic_store(&r->claim, CAP + 4);

    /* Frames 4.. are untouched and still readable. */
    rd2.pos = 4;
    TEST_EQ(ring_read(&rd2, f, 8), 0);
    TEST_CHECK(frames_are(f, 4, 8));

    TEST_EQ(ring_read(&rd, f, 8), -EPIPE);
    TEST_EQ(rd.dropped, CAP);
    TEST_EQ(rd.pos, CAP);
}

static void test_restart(void) {
    struct ring_writer w;
    struct ring_reader rd;
    struct frame f[CAP], zero[10];
    struct ring *r = fresh(&w, &rd);

    /* Crash mid-push: frames 20..29 claimed and half-written. */
    push(&w, 0, 20);
    atomic_store(&r->claim, 30);
    memset(g_buf + RING_ALIGN + 20 * FS, 0xA5, 10 * FS);

    TEST_EQ(ring_init(r, CAP, FS, 2, &w), 1);
    TEST_EQ(w.head, 30);
    TEST_EQ(atomic_load(&r->commit), 30);

    TEST_EQ(ring_read(&rd, f, 5), -ESTALE);
    TEST_EQ(rd.gen, 2);
    TEST_EQ(rd.pos, 30);
    TEST_EQ(rd.dropped, 0);
    push(&w, 30, 5);
    TEST_EQ(ring_read(&rd, f, 5), 0);
    TEST_CHECK(frames_are(f, 30, 5));

    /* The partial push was replaced with silence. */
    memset(zero, 0, sizeof zero);
    rd.pos = 20;
    TEST_EQ(ring_read(&rd, f, 10), 0);
    TEST_EQ(memcmp(f, zero, sizeof zero), 0);

    /* Inconsistent counters, or a different shape, start fresh. */
    atomic_store(&r->claim, 10);
    TEST_EQ(ring_init(r, CAP, FS, 3, &w), 0);
    TEST_EQ(atomic_load(&r->commit), 0);
    TEST_EQ(ring_init(r, CAP / 2, FS, 4, &w), 0);
    TEST_EQ(ring_init(r, CAP, FS, 5, &w), 0);
    TEST_EQ(ring_read(&rd, f, 1), -ESTALE);
    TEST_EQ(rd.pos, 0);
}

static int stress_writer(struct ring_writer *w) {
    struct frame f[STRESS_PUSH];

    for (uint64_t at = 0; at < STRESS_N; at += STRESS_PUSH) {
        uint32_t n = STRESS_N - at < STRESS_PUSH ? (uint32_t)(STRESS_N - at)
                                                 : STRESS_PUSH;
        make(f, at, n);
        if (ring_push(w, f, n) != 0)
            return 1;
    }
    return 0;
}

/* Writer and reader in separate processes over a MAP_SHARED mapping, as in
 * the real daemons. The ring is small so the writer laps the reader often;
 * every chunk the reader accepts must be exactly the frames it asked for. */
static void test_cross_process(void) {
    size_t len = ring_bytes(STRESS_CAP, FS);
    struct ring *r = mmap(NULL, len, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (r == MAP_FAILED) {
        perror("mmap");
        TEST_CHECK(0);
        return;
    }

    struct ring_writer w;
    struct ring_reader rd;
    TEST_EQ(ring_init(r, STRESS_CAP, FS, 1, &w), 0);
    TEST_EQ(ring_reader_attach(&rd, r, STRESS_CAP, FS), 0);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        TEST_CHECK(0);
        goto out_unmap;
    }
    if (pid == 0)
        _exit(stress_writer(&w));

    struct frame f[STRESS_READ];
    uint64_t got = 0;
    unsigned long idle = 0;
    int status = 0, reaped = 0;
    for (;;) {
        uint64_t pos = rd.pos;
        int rc = ring_read(&rd, f, STRESS_READ);
        if (rc == 0) {
            TEST_CHECK(frames_are(f, pos, STRESS_READ));
            got += STRESS_READ;
            continue;
        }
        if (rc == -EPIPE)
            continue;
        if (rc != -EAGAIN) {
            TEST_EQ(rc, -EAGAIN);
            break;
        }
        /* The writer is gone and everything it wrote has been drained. */
        if (reaped)
            break;
        if (++idle % 65536 != 0)
            continue;
        pid_t wr = waitpid(pid, &status, WNOHANG);
        if (wr < 0) {
            perror("waitpid");
            TEST_CHECK(0);
            goto out_unmap;
        }
        reaped = wr == pid;
    }

    if (!reaped && waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        TEST_CHECK(0);
        goto out_unmap;
    }
    TEST_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    TEST_EQ(got + rd.dropped, rd.pos);
    TEST_CHECK(rd.pos <= STRESS_N && STRESS_N - rd.pos < STRESS_READ);

out_unmap:
    if (munmap(r, len) != 0) {
        perror("munmap");
        TEST_CHECK(0);
    }
}

int main(void) {
    test_args();
    test_push_read();
    test_lap();
    test_inflight();
    test_restart();
    test_cross_process();
    return test_failures();
}
