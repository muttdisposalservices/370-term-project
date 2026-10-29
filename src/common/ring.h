/* Single-producer / single-consumer overwriting ring in shared memory
 * (issues.md C9). audiod writes interleaved (Source, System) frames every
 * period and never waits; analyzerd reads and drops frames when it falls
 * behind.
 *
 * Two monotonic 64-bit frame counters, written only by the writer:
 *   - claim:  frames the writer has started writing (stored before the data)
 *   - commit: frames fully written (stored after the data)
 * The reader copies frames below commit, then re-reads claim. Frame pos is
 * overwritten exactly when claim > pos + cap, so a copy torn by a writer
 * that lapped it (even one still mid-push) is detected and discarded. A
 * single post-write counter would miss the mid-push case.
 *
 * gen changes when audiod restarts (ring_init). The counters stay monotonic
 * across a restart, so the reader only has to notice the new gen and reset
 * its analysis window.
 *
 * The reader's copy can race the writer's; the claim check discards any
 * such copy (the usual seqlock-style read). No allocation, no syscalls: the
 * caller provides the mapping. */
#ifndef RING_H
#define RING_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Cross-process atomics must be lock-free (and therefore address-free). */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "ring needs lock-free 32-bit atomics");
_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2 && ATOMIC_LONG_LOCK_FREE == 2,
               "ring needs lock-free 64-bit atomics");

#define RING_MAGIC   0x52494e47u /* "RING" */
#define RING_VERSION 1u
#define RING_ALIGN   64u

/* Shared header. Frames follow at offset RING_ALIGN. */
struct ring {
    uint32_t magic;
    uint32_t version;
    uint32_t frame_size;
    uint32_t cap; /* frames, power of two */
    _Atomic uint32_t gen;
    _Atomic uint64_t claim;
    _Atomic uint64_t commit;
};

struct ring_writer {
    struct ring *r;
    uint64_t head;
    uint32_t cap;
    uint32_t frame_size;
};

struct ring_reader {
    struct ring *r;
    uint64_t pos;
    uint64_t dropped;
    uint32_t gen;
    uint32_t cap;
    uint32_t frame_size;
};

/* Bytes to map for cap_frames frames of frame_size bytes, or 0 if invalid. */
size_t ring_bytes(uint32_t cap_frames, uint32_t frame_size);

/* Writer side (the segment's creator). r must be RING_ALIGN-aligned and
 * ring_bytes() long. If r already holds a valid ring of the same shape, it
 * is resumed under the new gen (returns 1); otherwise it is initialised
 * fresh (returns 0). Returns -EINVAL on bad arguments. */
int ring_init(struct ring *r, uint32_t cap_frames, uint32_t frame_size,
              uint32_t gen, struct ring_writer *w);

/* RT-safe. Appends n frames (n <= cap). Returns 0 or -EINVAL. */
int ring_push(struct ring_writer *w, const void *frames, uint32_t n);

/* Reader side. Starts at the newest frame. Returns 0, -EINVAL (bad args/
 * alignment) or -EPROTO (bad magic/version/shape). */
int ring_reader_attach(struct ring_reader *rd, struct ring *r,
                       uint32_t cap_frames, uint32_t frame_size);

/* Copies the next n frames (1 <= n <= cap) into dst. Returns:
 *   0       copied; pos advanced by n
 *   -EAGAIN fewer than n frames written yet
 *   -EPIPE  the writer lapped the reader; frames lost are added to dropped
 *           and pos jumps to the newest frame
 *   -ESTALE the writer restarted; pos jumps to the newest frame
 *   -EINVAL bad n
 * On -EPIPE and -ESTALE the stream is discontinuous: reset any window. */
int ring_read(struct ring_reader *rd, void *dst, uint32_t n);

#endif
