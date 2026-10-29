/* Triple buffer (src/common/tbuf.c, issues.md C8). */
#define _DEFAULT_SOURCE

#include "test.h"
#include "tbuf.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define SLOT_SIZE  760 /* 19 biquads x 5 float64 */
#define SLOT_WORDS (SLOT_SIZE / sizeof(uint64_t))
#define STRESS_N   20000u

static _Alignas(TBUF_ALIGN) unsigned char g_buf[4096];

static void fill(void *slot, uint64_t v) {
    uint64_t *w = slot;
    for (size_t i = 0; i < SLOT_WORDS; i++)
        w[i] = v;
}

/* Returns the slot's value, or UINT64_MAX if its words disagree (torn). */
static uint64_t read_slot(const void *slot) {
    const uint64_t *w = slot;
    for (size_t i = 1; i < SLOT_WORDS; i++)
        if (w[i] != w[0])
            return UINT64_MAX;
    return w[0];
}

static uint32_t middle_of(const struct tbuf *tb) {
    return atomic_load(&((struct tbuf *)tb)->state) & 0x3u;
}

static void test_args(void) {
    struct tbuf *tb = (struct tbuf *)g_buf;
    struct tbuf *unaligned = (struct tbuf *)(g_buf + 8);
    struct tbuf_consumer c;
    struct tbuf_producer p;

    TEST_CHECK(tbuf_bytes(SLOT_SIZE) <= sizeof g_buf);
    TEST_EQ(tbuf_bytes(SLOT_SIZE) % TBUF_ALIGN, 0);
    TEST_EQ(tbuf_bytes(0), 0);
    TEST_EQ(tbuf_init(unaligned, SLOT_SIZE, 1, &c), -EINVAL);
    TEST_EQ(tbuf_init(tb, 0, 1, &c), -EINVAL);
    TEST_EQ(tbuf_init(tb, SLOT_SIZE, TBUF_GEN_MAX + 1, &c), -EINVAL);

    TEST_EQ(tbuf_init(tb, SLOT_SIZE, 1, &c), 0);
    TEST_EQ(tbuf_producer_attach(&p, tb, SLOT_SIZE + 8), -EPROTO);
    tb->magic = 0;
    TEST_EQ(tbuf_producer_attach(&p, tb, SLOT_SIZE), -EPROTO);
}

static void test_publish_poll(void) {
    struct tbuf *tb = (struct tbuf *)g_buf;
    struct tbuf_consumer c;
    struct tbuf_producer p;

    TEST_EQ(tbuf_init(tb, SLOT_SIZE, 1, &c), 0);
    TEST_EQ(tbuf_producer_attach(&p, tb, SLOT_SIZE), 0);
    TEST_EQ(tbuf_consumer_poll(&c), 0);
    TEST_EQ(read_slot(tbuf_consumer_slot(&c)), 0);

    fill(tbuf_producer_slot(&p), 7);
    TEST_EQ(tbuf_publish(&p), 0);
    TEST_EQ(tbuf_consumer_poll(&c), 1);
    TEST_EQ(read_slot(tbuf_consumer_slot(&c)), 7);
    TEST_EQ(tbuf_consumer_poll(&c), 0);

    /* Two publishes before a poll: the newer one wins. */
    fill(tbuf_producer_slot(&p), 8);
    TEST_EQ(tbuf_publish(&p), 0);
    fill(tbuf_producer_slot(&p), 9);
    TEST_EQ(tbuf_publish(&p), 0);
    TEST_EQ(tbuf_consumer_poll(&c), 1);
    TEST_EQ(read_slot(tbuf_consumer_slot(&c)), 9);
    TEST_EQ(tbuf_consumer_poll(&c), 0);
}

/* Random interleavings: slots stay a permutation, the consumer never sees
 * an older value than before, and a fresh producer recovers the same slot. */
static void test_interleave(void) {
    struct tbuf *tb = (struct tbuf *)g_buf;
    struct tbuf_consumer c;
    struct tbuf_producer p, p2;
    uint64_t seq = 0, published = 0, seen = 0;

    srand(370);
    TEST_EQ(tbuf_init(tb, SLOT_SIZE, 2, &c), 0);
    TEST_EQ(tbuf_producer_attach(&p, tb, SLOT_SIZE), 0);

    for (int i = 0; i < 10000; i++) {
        if (rand() & 1) {
            fill(tbuf_producer_slot(&p), ++seq);
            TEST_EQ(tbuf_publish(&p), 0);
            published = seq;
        } else if (tbuf_consumer_poll(&c)) {
            uint64_t v = read_slot(tbuf_consumer_slot(&c));
            TEST_EQ(v, published);
            TEST_CHECK(v > seen);
            seen = v;
        }
        uint32_t m = middle_of(tb);
        TEST_CHECK(p.back != c.front && p.back != m && c.front != m);

        if (i % 997 == 0) {
            TEST_EQ(tbuf_producer_attach(&p2, tb, SLOT_SIZE), 0);
            TEST_EQ(p2.back, p.back);
            TEST_EQ(p2.gen, p.gen);
        }
    }
}

static void test_restart(void) {
    struct tbuf *tb = (struct tbuf *)g_buf;
    struct tbuf_consumer c;
    struct tbuf_producer p;

    TEST_EQ(tbuf_init(tb, SLOT_SIZE, 3, &c), 0);
    TEST_EQ(tbuf_producer_attach(&p, tb, SLOT_SIZE), 0);

    /* front_shadow not yet updated after a swap: it equals middle. */
    uint32_t saved = atomic_load(&tb->front_shadow);
    atomic_store(&tb->front_shadow, middle_of(tb));
    struct tbuf_producer p2;
    TEST_EQ(tbuf_producer_attach(&p2, tb, SLOT_SIZE), -EAGAIN);
    atomic_store(&tb->front_shadow, saved);

    /* Consumer re-inits with a new gen: the old producer is rejected. */
    TEST_EQ(tbuf_init(tb, SLOT_SIZE, 4, &c), 0);
    fill(tbuf_producer_slot(&p), 5);
    TEST_EQ(tbuf_publish(&p), -ESTALE);
    TEST_EQ(tbuf_consumer_poll(&c), 0);

    TEST_EQ(tbuf_producer_attach(&p, tb, SLOT_SIZE), 0);
    TEST_EQ(p.gen, 4);
    fill(tbuf_producer_slot(&p), 6);
    TEST_EQ(tbuf_publish(&p), 0);
    TEST_EQ(tbuf_consumer_poll(&c), 1);
    TEST_EQ(read_slot(tbuf_consumer_slot(&c)), 6);
}

static int stress_producer(struct tbuf *tb) {
    struct tbuf_producer p;
    int rc;

    while ((rc = tbuf_producer_attach(&p, tb, SLOT_SIZE)) == -EAGAIN)
        ;
    if (rc != 0)
        return 1;
    for (uint64_t v = 1; v <= STRESS_N; v++) {
        fill(tbuf_producer_slot(&p), v);
        if (tbuf_publish(&p) != 0)
            return 1;
    }
    return 0;
}

/* Returns 1 once the final value has been seen. */
static int stress_check(struct tbuf_consumer *c, uint64_t *seen) {
    uint64_t v = read_slot(tbuf_consumer_slot(c));
    TEST_CHECK(v != UINT64_MAX);
    TEST_CHECK(v > *seen);
    *seen = v;
    return v == STRESS_N;
}

/* Producer and consumer in separate processes over a MAP_SHARED mapping,
 * as in the real daemons. Any torn slot shows up as mixed words. */
static void test_cross_process(void) {
    size_t len = tbuf_bytes(SLOT_SIZE);
    struct tbuf *tb = mmap(NULL, len, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (tb == MAP_FAILED) {
        perror("mmap");
        TEST_CHECK(0);
        return;
    }

    struct tbuf_consumer c;
    TEST_EQ(tbuf_init(tb, SLOT_SIZE, 5, &c), 0);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        TEST_CHECK(0);
        goto out_unmap;
    }
    if (pid == 0)
        _exit(stress_producer(tb));

    uint64_t seen = 0;
    unsigned long idle = 0;
    int status = 0, reaped = 0, done = 0;
    while (!done) {
        if (tbuf_consumer_poll(&c)) {
            done = stress_check(&c, &seen);
            continue;
        }
        if (++idle % 65536 != 0)
            continue;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r < 0) {
            perror("waitpid");
            TEST_CHECK(0);
            goto out_unmap;
        }
        if (r == pid) {
            /* Producer is gone: its last publish must still be pending. */
            reaped = 1;
            done = tbuf_consumer_poll(&c) && stress_check(&c, &seen);
            TEST_CHECK(done);
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
    if (munmap(tb, len) != 0) {
        perror("munmap");
        TEST_CHECK(0);
    }
}

int main(void) {
    test_args();
    test_publish_poll();
    test_interleave();
    test_restart();
    test_cross_process();
    return test_failures();
}
