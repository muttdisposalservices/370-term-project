/* Single-producer / single-consumer triple buffer in shared memory
 * (issues.md C8). Used for the analyzerd->audiod coeff block and the
 * ctld->audiod param block.
 *
 * One atomic word, `state`, holds {gen:24 | fresh:1 | idx:2}. The producer
 * privately owns one slot (back), the consumer one (front), and state.idx
 * names the third (middle), so the producer always has a free slot.
 *   - publish: fill back, then swap it into middle with FRESH set. An unread
 *     middle is overwritten (latest wins).
 *   - poll: one relaxed load; only if FRESH is set, swap front with middle.
 * The consumer copies its front slot into private state on each swap and
 * ramps between private buffers, so it never holds a second slot.
 *
 * Restart recovery: the consumer (audiod) creates the block and mirrors its
 * front index into front_shadow, so a restarted producer can recompute its
 * back slot. A consumer restart re-inits with a new gen; a producer still
 * on the old gen gets -ESTALE from tbuf_publish and must reattach.
 *
 * No allocation, no syscalls: the caller provides the mapping. */
#ifndef TBUF_H
#define TBUF_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Cross-process atomics must be lock-free (and therefore address-free). */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "tbuf needs lock-free 32-bit atomics");

#define TBUF_MAGIC   0x54425546u /* "TBUF" */
#define TBUF_VERSION 1u
#define TBUF_ALIGN   64u
#define TBUF_GEN_MAX 0xFFFFFFu

/* Shared header. Slots follow at TBUF_ALIGN-aligned offsets. */
struct tbuf {
    uint32_t magic;
    uint32_t version;
    uint32_t slot_size;
    uint32_t stride;
    _Atomic uint32_t state;
    _Atomic uint32_t front_shadow;
};

struct tbuf_producer {
    struct tbuf *tb;
    uint32_t back;
    uint32_t gen;
};

struct tbuf_consumer {
    struct tbuf *tb;
    uint32_t front;
    uint32_t gen;
};

/* Bytes to map for a block whose slots hold slot_size bytes. */
size_t tbuf_bytes(size_t slot_size);

/* Consumer side (the segment's creator). tb must be TBUF_ALIGN-aligned and
 * tbuf_bytes(slot_size) long. Zeroes all slots. Returns 0 or -EINVAL. */
int tbuf_init(struct tbuf *tb, size_t slot_size, uint32_t gen,
              struct tbuf_consumer *c);

/* RT-safe. Returns 1 if a new value was swapped into front, else 0. */
int tbuf_consumer_poll(struct tbuf_consumer *c);
const void *tbuf_consumer_slot(const struct tbuf_consumer *c);

/* Producer side. Returns 0, -EINVAL (bad args/alignment), -EPROTO (bad
 * magic/version/slot_size) or -EAGAIN (consumer mid-swap: retry). */
int tbuf_producer_attach(struct tbuf_producer *p, struct tbuf *tb,
                         size_t slot_size);

/* Fill this, then tbuf_publish(). Valid until the next publish. */
void *tbuf_producer_slot(const struct tbuf_producer *p);

/* Returns 0, or -ESTALE if the consumer re-initialised the block. */
int tbuf_publish(struct tbuf_producer *p);

#endif
