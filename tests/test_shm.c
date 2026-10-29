/* Segment lifecycle (src/common/shm.c, resolved_issues.md C12). Uses real
 * POSIX shm; every name carries the pid and is unlinked before exit. */
#define _DEFAULT_SOURCE

#include "test.h"
#include "ring.h"
#include "seqlock.h"
#include "shm.h"
#include "tbuf.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define BYTES 4096u
#define MODE  0660u
#define CAP   64u
#define FS    8u
#define DATA  64u

static char g_name[64];

static const char *name_for(const char *tag) {
    snprintf(g_name, sizeof g_name, "/dspunit-test-%ld-%s", (long)getpid(), tag);
    (void)shm_unlink(g_name);
    return g_name;
}

/* Returns st_mode/st_size of name via a fresh open, or -1 on error. */
static int stat_name(const char *name, struct stat *st) {
    int fd = shm_open(name, O_RDONLY, 0);
    if (fd < 0)
        return -1;
    int ret = fstat(fd, st);
    if (close(fd) != 0)
        ret = -1;
    return ret;
}

static void cleanup(const char *name, struct shm_seg *a, struct shm_seg *b) {
    if (a)
        TEST_EQ(shm_seg_close(a), 0);
    if (b)
        TEST_EQ(shm_seg_close(b), 0);
    if (shm_unlink(name) != 0 && errno != ENOENT) {
        perror("shm_unlink");
        TEST_CHECK(0);
    }
}

static void test_args(void) {
    struct shm_seg s = {0};
    const char *name = name_for("args");

    TEST_EQ(shm_seg_create(NULL, name, BYTES, MODE), -EINVAL);
    TEST_EQ(shm_seg_create(&s, NULL, BYTES, MODE), -EINVAL);
    TEST_EQ(shm_seg_create(&s, name, 0, MODE), -EINVAL);
    TEST_EQ(shm_seg_attach(&s, name, 0, 0), -EINVAL);
    TEST_EQ(shm_seg_replaced(&s), -EINVAL);
    TEST_EQ(shm_seg_close(&s), 0);
    TEST_EQ(shm_seg_close(NULL), 0);
}

static void test_create(void) {
    struct shm_seg a = {0}, b = {0};
    struct stat st;
    const char *name = name_for("create");

    /* Missing: created at the size, with mode not masked by umask. */
    mode_t old = umask(077);
    TEST_EQ(shm_seg_create(&a, name, BYTES, MODE), 1);
    umask(old);
    TEST_CHECK(a.base != NULL);
    TEST_EQ(stat_name(name, &st), 0);
    TEST_EQ((uint64_t)st.st_size, BYTES);
    TEST_EQ(st.st_mode & 0777u, MODE);
    TEST_EQ(((unsigned char *)a.base)[BYTES - 1], 0);

    /* Existing, right size: kept, same object, contents intact. */
    ((unsigned char *)a.base)[7] = 0xA5;
    TEST_EQ(shm_seg_create(&b, name, BYTES, MODE), 0);
    TEST_EQ(b.ino, a.ino);
    TEST_EQ(((unsigned char *)b.base)[7], 0xA5);
    TEST_EQ(shm_seg_replaced(&a), 0);

    cleanup(name, &a, &b);
}

static void test_wrong_size(void) {
    struct shm_seg a = {0}, b = {0};
    struct stat st;
    const char *name = name_for("size");

    TEST_EQ(shm_seg_create(&a, name, BYTES, MODE), 1);
    ((unsigned char *)a.base)[7] = 0xA5;

    /* Recreated as a new object; the old mapping stays valid. */
    TEST_EQ(shm_seg_create(&b, name, 2 * BYTES, MODE), 1);
    TEST_CHECK(b.ino != a.ino);
    TEST_EQ(stat_name(name, &st), 0);
    TEST_EQ((uint64_t)st.st_size, 2 * BYTES);
    TEST_EQ(((unsigned char *)a.base)[7], 0xA5);
    TEST_EQ(((unsigned char *)b.base)[7], 0);
    TEST_EQ(shm_seg_replaced(&a), 1);
    TEST_EQ(shm_seg_replaced(&b), 0);

    cleanup(name, &a, &b);
}

static void test_unopenable(void) {
    struct shm_seg s = {0};
    struct stat st;
    const char *name = name_for("perm");

    /* Left behind with mode 0000, as by a manual run with another umask. */
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0);
    if (fd < 0) {
        perror("shm_open");
        TEST_CHECK(0);
        return;
    }
    TEST_EQ(ftruncate(fd, BYTES), 0);
    TEST_EQ(close(fd), 0);

    /* root ignores the mode, so the segment is simply kept. */
    TEST_EQ(shm_seg_create(&s, name, BYTES, MODE), geteuid() == 0 ? 0 : 1);
    if (geteuid() != 0) {
        TEST_EQ(stat_name(name, &st), 0);
        TEST_EQ(st.st_mode & 0777u, MODE);
    }

    cleanup(name, &s, NULL);
}

static void test_attach(void) {
    struct shm_seg a = {0}, r = {0};
    const char *name = name_for("attach");

    TEST_EQ(shm_seg_attach(&r, name, BYTES, 0), -ENOENT);
    TEST_EQ(shm_seg_replaced(&(struct shm_seg){.name = name}), 1);

    TEST_EQ(shm_seg_create(&a, name, BYTES, MODE), 1);
    TEST_EQ(shm_seg_attach(&r, name, 2 * BYTES, 0), -EPROTO);
    TEST_EQ(shm_seg_attach(&r, name, BYTES, 1), 0);
    TEST_EQ(r.ino, a.ino);
    TEST_EQ(shm_seg_replaced(&r), 0);

    ((unsigned char *)a.base)[3] = 0x5A;
    TEST_EQ(((unsigned char *)r.base)[3], 0x5A);

    cleanup(name, &a, &r);
}

/* One audiod start: create or repair, then init. Returns the init result. */
static int start_ring(struct shm_seg *s, const char *name, uint32_t gen,
                      struct ring_writer *w) {
    if (shm_seg_create(s, name, ring_bytes(CAP, FS), MODE) < 0)
        return -1;
    return ring_init(s->base, CAP, FS, gen, w);
}

/* C12: a corrupt header no longer means exit and restart forever. Each
 * restart maps the same object and the init repairs it. */
static void test_corrupt_header(void) {
    struct shm_seg w1 = {0}, w2 = {0}, w3 = {0}, rs = {0};
    struct ring_writer w;
    struct ring_reader rd;
    unsigned char f[FS] = {1, 2, 3, 4, 5, 6, 7, 8}, got[FS];
    const char *name = name_for("hdr");

    TEST_EQ(start_ring(&w1, name, 1, &w), 0);
    TEST_EQ(shm_seg_attach(&rs, name, ring_bytes(CAP, FS), 0), 0);
    TEST_EQ(ring_reader_attach(&rd, rs.base, CAP, FS), 0);

    memset(w1.base, 0xEE, RING_ALIGN);
    TEST_EQ(start_ring(&w2, name, shm_next_gen(1, 1, 0), &w), 0);
    TEST_EQ(start_ring(&w3, name, shm_next_gen(1, 2, 0), &w), 1);
    TEST_EQ(w3.ino, w1.ino);
    TEST_EQ(shm_seg_replaced(&rs), 0);

    TEST_EQ(ring_push(&w, f, 1), 0);
    TEST_EQ(ring_read(&rd, got, 1), -ESTALE);
    TEST_EQ(ring_push(&w, f, 1), 0);
    TEST_EQ(ring_read(&rd, got, 1), 0);
    TEST_EQ(memcmp(got, f, FS), 0);

    cleanup(name, &w1, &w2);
    TEST_EQ(shm_seg_close(&w3), 0);
    TEST_EQ(shm_seg_close(&rs), 0);
}

static void test_corrupt_header_seqlock(void) {
    struct shm_seg ws = {0}, rs = {0};
    struct seqlock_writer w;
    struct seqlock_reader rd;
    unsigned char d[DATA], got[DATA];
    size_t len = seqlock_bytes(DATA);
    const char *name = name_for("hdrsl");

    TEST_EQ(shm_seg_create(&ws, name, len, MODE), 1);
    TEST_EQ(seqlock_init(ws.base, DATA, 1, &w), 0);
    TEST_EQ(shm_seg_attach(&rs, name, len, 0), 0);
    TEST_EQ(seqlock_reader_attach(&rd, rs.base, DATA), 0);

    memset(ws.base, 0xEE, SEQLOCK_ALIGN);
    TEST_EQ(shm_seg_close(&ws), 0);

    /* Restart: the old gen is unreadable, so the seed is used. */
    struct seqlock_reader peek;
    TEST_EQ(shm_seg_create(&ws, name, len, MODE), 0);
    int have_prev = seqlock_reader_attach(&peek, ws.base, DATA) == 0;
    TEST_EQ(have_prev, 0);
    TEST_EQ(seqlock_init(ws.base, DATA, shm_next_gen(have_prev, 0, 7), &w), 0);

    memset(d, 0x42, sizeof d);
    seqlock_write(&w, d);
    TEST_EQ(seqlock_read(&rd, got), -ESTALE);
    TEST_EQ(seqlock_read(&rd, got), 0);
    TEST_EQ(memcmp(got, d, DATA), 0);

    cleanup(name, &ws, &rs);
}

static void test_corrupt_header_tbuf(void) {
    struct shm_seg cs = {0}, ps = {0};
    struct tbuf_consumer c;
    struct tbuf_producer p;
    size_t len = tbuf_bytes(DATA);
    const char *name = name_for("hdrtb");

    TEST_EQ(shm_seg_create(&cs, name, len, MODE), 1);
    TEST_EQ(tbuf_init(cs.base, DATA, 1, &c), 0);
    TEST_EQ(shm_seg_attach(&ps, name, len, 1), 0);
    TEST_EQ(tbuf_producer_attach(&p, ps.base, DATA), 0);

    memset(cs.base, 0xEE, TBUF_ALIGN);
    TEST_EQ(shm_seg_close(&cs), 0);

    TEST_EQ(shm_seg_create(&cs, name, len, MODE), 0);
    TEST_EQ(tbuf_init(cs.base, DATA, shm_next_gen(1, 1, 0), &c), 0);

    memset(tbuf_producer_slot(&p), 0x42, DATA);
    TEST_EQ(tbuf_publish(&p), -ESTALE);
    TEST_EQ(tbuf_producer_attach(&p, ps.base, DATA), 0);
    memset(tbuf_producer_slot(&p), 0x42, DATA);
    TEST_EQ(tbuf_publish(&p), 0);
    TEST_EQ(tbuf_consumer_poll(&c), 1);
    TEST_EQ(((const unsigned char *)tbuf_consumer_slot(&c))[DATA - 1], 0x42);

    cleanup(name, &cs, &ps);
}

/* C12: a segment of the wrong size is recreated; the old reader notices
 * by name and attaches to the new object. */
static void test_corrupt_size(void) {
    struct shm_seg ws = {0}, rs = {0};
    struct ring_writer w;
    struct ring_reader rd;
    unsigned char f[FS] = {8, 7, 6, 5, 4, 3, 2, 1}, got[FS];
    size_t len = ring_bytes(CAP, FS);
    const char *name = name_for("trunc");

    TEST_EQ(start_ring(&ws, name, 1, &w), 0);
    TEST_EQ(shm_seg_attach(&rs, name, len, 0), 0);
    TEST_EQ(ring_reader_attach(&rd, rs.base, CAP, FS), 0);
    TEST_EQ(shm_seg_close(&ws), 0);

    /* Grown, not shrunk, so the reader's old mapping can't SIGBUS here. */
    int fd = shm_open(name, O_RDWR, 0);
    TEST_CHECK(fd >= 0);
    if (fd >= 0) {
        TEST_EQ(ftruncate(fd, (off_t)(len + BYTES)), 0);
        TEST_EQ(close(fd), 0);
    }

    TEST_EQ(start_ring(&ws, name, shm_next_gen(1, 1, 0), &w), 0);
    TEST_CHECK(ws.ino != rs.ino);

    TEST_EQ(shm_seg_replaced(&rs), 1);
    TEST_EQ(shm_seg_close(&rs), 0);
    TEST_EQ(shm_seg_attach(&rs, name, len, 0), 0);
    TEST_EQ(ring_reader_attach(&rd, rs.base, CAP, FS), 0);
    TEST_EQ(shm_seg_replaced(&rs), 0);

    TEST_EQ(ring_push(&w, f, 1), 0);
    TEST_EQ(ring_read(&rd, got, 1), 0);
    TEST_EQ(memcmp(got, f, FS), 0);

    cleanup(name, &ws, &rs);
}

static void test_next_gen(void) {
    TEST_EQ(shm_next_gen(1, 5, 0), 6);
    TEST_EQ(shm_next_gen(1, TBUF_GEN_MAX, 0), 0);
    TEST_EQ(shm_next_gen(0, 5, 0x12345678u), 0x345678u);
    TEST_CHECK(shm_next_gen(0, 0, UINT64_MAX) <= TBUF_GEN_MAX);
}

int main(void) {
    test_args();
    test_create();
    test_wrong_size();
    test_unopenable();
    test_attach();
    test_corrupt_header();
    test_corrupt_header_seqlock();
    test_corrupt_header_tbuf();
    test_corrupt_size();
    test_next_gen();
    return test_failures();
}
