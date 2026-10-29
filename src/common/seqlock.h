/* Single-writer seqlock in shared memory (issues.md C10). audiod writes
 * the status block every period and never waits; ctld and audiod's
 * watchdog thread read it and retry if a write overlapped their copy.
 *
 * seq is even while the data is stable and odd while a write is in
 * progress. The writer stores seq+1, writes the data, then stores seq+2.
 * The reader loads seq, copies the data, and re-loads seq; the copy is
 * kept only if both loads match and are even. Only the writer stores
 * seq, so it needs no read-modify-write.
 *
 * gen changes when audiod restarts (seqlock_init). seq stays monotonic
 * across a restart, and a write left open by a crashed writer is closed
 * by the next init.
 *
 * The reader's copy can race the writer's; the seq check discards any
 * such copy (as in ring.h). No allocation, no syscalls: the caller
 * provides the mapping. */
#ifndef SEQLOCK_H
#define SEQLOCK_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Cross-process atomics must be lock-free (and therefore address-free). */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "seqlock needs lock-free 32-bit atomics");
_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2 && ATOMIC_LONG_LOCK_FREE == 2,
               "seqlock needs lock-free 64-bit atomics");

#define SEQLOCK_MAGIC   0x5345514cu /* "SEQL" */
#define SEQLOCK_VERSION 1u
#define SEQLOCK_ALIGN   64u
#define SEQLOCK_TRIES   64

/* Shared header. Data follows at offset SEQLOCK_ALIGN. */
struct seqlock {
    uint32_t magic;
    uint32_t version;
    uint32_t data_size;
    _Atomic uint32_t gen;
    _Atomic uint64_t seq;
};

struct seqlock_writer {
    struct seqlock *sl;
    uint64_t seq;
    uint32_t data_size;
};

struct seqlock_reader {
    struct seqlock *sl;
    uint32_t gen;
    uint32_t data_size;
};

/* Bytes to map for data_size bytes of data, or 0 if invalid. */
size_t seqlock_bytes(size_t data_size);

/* Writer side (the segment's creator). sl must be SEQLOCK_ALIGN-aligned
 * and seqlock_bytes() long. The data starts zeroed. If sl already holds a
 * valid seqlock of the same size, it is resumed under the new gen
 * (returns 1); otherwise it is initialised fresh (returns 0). Returns
 * -EINVAL on bad arguments. */
int seqlock_init(struct seqlock *sl, size_t data_size, uint32_t gen,
                 struct seqlock_writer *w);

/* RT-safe. Copies data_size bytes from data into the block. */
void seqlock_write(struct seqlock_writer *w, const void *data);

/* Reader side. Returns 0, -EINVAL (bad args/alignment) or -EPROTO (bad
 * magic/version/size). */
int seqlock_reader_attach(struct seqlock_reader *rd, struct seqlock *sl,
                          size_t data_size);

/* Copies a consistent snapshot into dst. Returns:
 *   0       copied
 *   -EAGAIN a write overlapped every try (or a writer died mid-write)
 *   -ESTALE the writer restarted; dst is not valid, read again
 * On -ESTALE, counters in the data have restarted from zero. */
int seqlock_read(struct seqlock_reader *rd, void *dst);

#endif
