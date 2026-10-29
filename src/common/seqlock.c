#include "seqlock.h"

#include <errno.h>
#include <string.h>

_Static_assert(sizeof(struct seqlock) <= SEQLOCK_ALIGN, "seqlock header exceeds one line");

static int size_ok(size_t data_size) {
    return data_size > 0 && data_size <= UINT32_MAX - SEQLOCK_ALIGN;
}

static unsigned char *data_of(const struct seqlock *sl) {
    return (unsigned char *)sl + SEQLOCK_ALIGN;
}

size_t seqlock_bytes(size_t data_size) {
    if (!size_ok(data_size))
        return 0;
    return SEQLOCK_ALIGN + data_size;
}

int seqlock_init(struct seqlock *sl, size_t data_size, uint32_t gen,
                 struct seqlock_writer *w) {
    if (!sl || !w || !size_ok(data_size) || (uintptr_t)sl % SEQLOCK_ALIGN != 0)
        return -EINVAL;

    w->sl = sl;
    w->data_size = (uint32_t)data_size;

    if (sl->magic == SEQLOCK_MAGIC) {
        atomic_thread_fence(memory_order_acquire);
        if (sl->version == SEQLOCK_VERSION && sl->data_size == data_size) {
            /* Resume as one write. seq | 1 also closes a write left open
             * by a crashed writer; a reader mid-copy sees seq move. */
            uint64_t odd = atomic_load_explicit(&sl->seq, memory_order_relaxed) | 1u;
            atomic_store_explicit(&sl->seq, odd, memory_order_relaxed);
            atomic_thread_fence(memory_order_release);

            memset(data_of(sl), 0, data_size);
            atomic_store_explicit(&sl->gen, gen, memory_order_relaxed);

            w->seq = odd + 1;
            atomic_store_explicit(&sl->seq, w->seq, memory_order_release);
            return 1;
        }
    }

    sl->magic = 0;
    sl->version = SEQLOCK_VERSION;
    sl->data_size = (uint32_t)data_size;
    memset(data_of(sl), 0, data_size);
    atomic_store_explicit(&sl->gen, gen, memory_order_relaxed);
    atomic_store_explicit(&sl->seq, 0, memory_order_relaxed);

    /* Magic last: an attacher that sees it also sees the fields above. */
    atomic_thread_fence(memory_order_release);
    sl->magic = SEQLOCK_MAGIC;

    w->seq = 0;
    return 0;
}

void seqlock_write(struct seqlock_writer *w, const void *data) {
    struct seqlock *sl = w->sl;

    /* Odd before the data: a reader that copied any of the new bytes then
     * re-loads a seq that differs from its first load. */
    atomic_store_explicit(&sl->seq, w->seq + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);

    memcpy(data_of(sl), data, w->data_size);

    w->seq += 2;
    atomic_store_explicit(&sl->seq, w->seq, memory_order_release);
}

int seqlock_reader_attach(struct seqlock_reader *rd, struct seqlock *sl,
                          size_t data_size) {
    if (!rd || !sl || !size_ok(data_size) || (uintptr_t)sl % SEQLOCK_ALIGN != 0)
        return -EINVAL;
    if (sl->magic != SEQLOCK_MAGIC)
        return -EPROTO;
    atomic_thread_fence(memory_order_acquire);
    if (sl->version != SEQLOCK_VERSION || sl->data_size != data_size)
        return -EPROTO;

    /* The size is cached so a corrupted header can't steer a later copy. */
    rd->sl = sl;
    rd->data_size = (uint32_t)data_size;
    rd->gen = atomic_load_explicit(&sl->gen, memory_order_acquire);
    return 0;
}

int seqlock_read(struct seqlock_reader *rd, void *dst) {
    struct seqlock *sl = rd->sl;

    for (int i = 0; i < SEQLOCK_TRIES; i++) {
        uint64_t s1 = atomic_load_explicit(&sl->seq, memory_order_acquire);
        if (s1 & 1u)
            continue;
        memcpy(dst, data_of(sl), rd->data_size);

        /* Copy before the re-load. gen is only written inside a write, so
         * s1 == s2 means gen is the one the copy was taken under. If the
         * two loads reorder, the worst case is an early -ESTALE, which
         * promises no data. */
        atomic_thread_fence(memory_order_acquire);
        uint32_t gen = atomic_load_explicit(&sl->gen, memory_order_relaxed);
        uint64_t s2 = atomic_load_explicit(&sl->seq, memory_order_relaxed);
        if (s1 != s2)
            continue;
        if (gen != rd->gen) {
            rd->gen = gen;
            return -ESTALE;
        }
        return 0;
    }
    return -EAGAIN;
}
