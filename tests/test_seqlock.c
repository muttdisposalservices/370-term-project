/* Seqlock (src/common/seqlock.c, issues.md C10). */
#define _DEFAULT_SOURCE

#include "test.h"
#include "seqlock.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define DATA_SIZE  64 /* about the size of the status block */
#define DATA_WORDS (DATA_SIZE / sizeof(uint64_t))
#define STRESS_N   200000u

static _Alignas(SEQLOCK_ALIGN) unsigned char g_buf[256];

static void fill(uint64_t *w, uint64_t v) {
    for (size_t i = 0; i < DATA_WORDS; i++)
        w[i] = v;
}

/* Returns the snapshot's value, or UINT64_MAX if its words disagree (torn). */
static uint64_t value_of(const uint64_t *w) {
    for (size_t i = 1; i < DATA_WORDS; i++)
        if (w[i] != w[0])
            return UINT64_MAX;
    return w[0];
}

static void write_value(struct seqlock_writer *w, uint64_t v) {
    uint64_t data[DATA_WORDS];
    fill(data, v);
    seqlock_write(w, data);
}

/* Returns the value read, or UINT64_MAX if the read failed or was torn. */
static uint64_t read_value(struct seqlock_reader *rd) {
    uint64_t data[DATA_WORDS];
    if (seqlock_read(rd, data) != 0)
        return UINT64_MAX;
    return value_of(data);
}

static void test_args(void) {
    struct seqlock *sl = (struct seqlock *)g_buf;
    struct seqlock *unaligned = (struct seqlock *)(g_buf + 8);
    struct seqlock_writer w;
    struct seqlock_reader rd;

    TEST_CHECK(seqlock_bytes(DATA_SIZE) <= sizeof g_buf);
    TEST_EQ(seqlock_bytes(0), 0);
    TEST_EQ(seqlock_init(unaligned, DATA_SIZE, 1, &w), -EINVAL);
    TEST_EQ(seqlock_init(sl, 0, 1, &w), -EINVAL);

    memset(g_buf, 0, sizeof g_buf);
    TEST_EQ(seqlock_init(sl, DATA_SIZE, 1, &w), 0);
    TEST_EQ(seqlock_reader_attach(&rd, unaligned, DATA_SIZE), -EINVAL);
    TEST_EQ(seqlock_reader_attach(&rd, sl, DATA_SIZE + 8), -EPROTO);
    sl->magic = 0;
    TEST_EQ(seqlock_reader_attach(&rd, sl, DATA_SIZE), -EPROTO);
}

static void test_write_read(void) {
    struct seqlock *sl = (struct seqlock *)g_buf;
    struct seqlock_writer w;
    struct seqlock_reader rd;

    memset(g_buf, 0xAB, sizeof g_buf);
    TEST_EQ(seqlock_init(sl, DATA_SIZE, 1, &w), 0);
    TEST_EQ(seqlock_reader_attach(&rd, sl, DATA_SIZE), 0);
    TEST_EQ(read_value(&rd), 0);

    write_value(&w, 7);
    TEST_EQ(atomic_load(&sl->seq) & 1u, 0);
    TEST_EQ(read_value(&rd), 7);

    /* Two writes before a read: the newer one wins. */
    write_value(&w, 8);
    write_value(&w, 9);
    TEST_EQ(atomic_load(&sl->seq) & 1u, 0);
    TEST_EQ(read_value(&rd), 9);
}

static void test_writer_busy(void) {
    struct seqlock *sl = (struct seqlock *)g_buf;
    struct seqlock_writer w;
    struct seqlock_reader rd;
    uint64_t data[DATA_WORDS];

    memset(g_buf, 0, sizeof g_buf);
    TEST_EQ(seqlock_init(sl, DATA_SIZE, 1, &w), 0);
    TEST_EQ(seqlock_reader_attach(&rd, sl, DATA_SIZE), 0);
    write_value(&w, 3);

    /* A writer mid-write: seq odd. The reader gives up, it doesn't wait. */
    uint64_t saved = atomic_load(&sl->seq);
    atomic_store(&sl->seq, saved + 1);
    TEST_EQ(seqlock_read(&rd, data), -EAGAIN);
    atomic_store(&sl->seq, saved);
    TEST_EQ(read_value(&rd), 3);
}

static void test_restart(void) {
    struct seqlock *sl = (struct seqlock *)g_buf;
    struct seqlock_writer w;
    struct seqlock_reader rd;
    uint64_t data[DATA_WORDS];

    memset(g_buf, 0, sizeof g_buf);
    TEST_EQ(seqlock_init(sl, DATA_SIZE, 2, &w), 0);
    TEST_EQ(seqlock_reader_attach(&rd, sl, DATA_SIZE), 0);
    write_value(&w, 5);

    /* Writer crashed mid-write: seq left odd. */
    uint64_t before = atomic_load(&sl->seq);
    atomic_store(&sl->seq, before + 1);
    TEST_EQ(seqlock_read(&rd, data), -EAGAIN);

    /* Restart resumes the segment under a new gen and closes the write. */
    TEST_EQ(seqlock_init(sl, DATA_SIZE, 3, &w), 1);
    uint64_t after = atomic_load(&sl->seq);
    TEST_EQ(after & 1u, 0);
    TEST_CHECK(after > before);
    TEST_EQ(seqlock_read(&rd, data), -ESTALE);
    TEST_EQ(rd.gen, 3);
    TEST_EQ(read_value(&rd), 0);

    write_value(&w, 6);
    TEST_EQ(read_value(&rd), 6);

    /* A different size is initialised fresh; the old reader sees the new
     * gen and can't reattach at the old size. */
    TEST_CHECK(seqlock_bytes(2 * DATA_SIZE) <= sizeof g_buf);
    TEST_EQ(seqlock_init(sl, 2 * DATA_SIZE, 4, &w), 0);
    TEST_EQ(seqlock_read(&rd, data), -ESTALE);
    TEST_EQ(seqlock_reader_attach(&rd, sl, DATA_SIZE), -EPROTO);
    TEST_EQ(seqlock_reader_attach(&rd, sl, 2 * DATA_SIZE), 0);
}

static int stress_writer(struct seqlock_writer *w) {
    for (uint64_t v = 1; v <= STRESS_N; v++)
        write_value(w, v);
    return 0;
}

/* Returns 1 once the final value has been seen, -1 on an unexpected
 * error, else 0. -EAGAIN (a busy writer) is allowed. */
static int stress_check(struct seqlock_reader *rd, uint64_t *seen) {
    uint64_t data[DATA_WORDS];
    int rc = seqlock_read(rd, data);
    if (rc == -EAGAIN)
        return 0;
    if (rc != 0) {
        TEST_EQ(rc, 0);
        return -1;
    }
    uint64_t v = value_of(data);
    TEST_CHECK(v != UINT64_MAX);
    TEST_CHECK(v >= *seen);
    *seen = v;
    return v == STRESS_N;
}

/* Writer and reader in separate processes over a MAP_SHARED mapping, as
 * in the real daemons. Any torn snapshot shows up as mixed words. */
static void test_cross_process(void) {
    size_t len = seqlock_bytes(DATA_SIZE);
    struct seqlock *sl = mmap(NULL, len, PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sl == MAP_FAILED) {
        perror("mmap");
        TEST_CHECK(0);
        return;
    }

    struct seqlock_writer w;
    struct seqlock_reader rd;
    TEST_EQ(seqlock_init(sl, DATA_SIZE, 5, &w), 0);
    TEST_EQ(seqlock_reader_attach(&rd, sl, DATA_SIZE), 0);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        TEST_CHECK(0);
        goto out_unmap;
    }
    if (pid == 0)
        _exit(stress_writer(&w));

    uint64_t seen = 0;
    unsigned long iter = 0;
    int status = 0, reaped = 0, done = 0;
    while (!done) {
        done = stress_check(&rd, &seen);
        if (done < 0)
            break;
        if (done || ++iter % 65536 != 0)
            continue;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r < 0) {
            perror("waitpid");
            TEST_CHECK(0);
            goto out_unmap;
        }
        if (r == pid) {
            /* Writer is gone: its last write must be readable. */
            reaped = 1;
            done = stress_check(&rd, &seen);
            TEST_EQ(done, 1);
            break;
        }
    }

    if (!reaped && waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        TEST_CHECK(0);
        goto out_unmap;
    }
    TEST_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    TEST_EQ(seen, STRESS_N);

out_unmap:
    if (munmap(sl, len) != 0) {
        perror("munmap");
        TEST_CHECK(0);
    }
}

int main(void) {
    test_args();
    test_write_read();
    test_writer_busy();
    test_restart();
    test_cross_process();
    return test_failures();
}
