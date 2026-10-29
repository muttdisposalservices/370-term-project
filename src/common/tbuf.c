#include "tbuf.h"

#include <errno.h>
#include <string.h>

#define TBUF_IDX_MASK 0x3u
#define TBUF_FRESH    0x4u
#define TBUF_GEN_SHIFT 8

_Static_assert(sizeof(struct tbuf) <= TBUF_ALIGN, "tbuf header exceeds one line");

static uint32_t stride_for(size_t slot_size) {
    return (uint32_t)((slot_size + TBUF_ALIGN - 1) / TBUF_ALIGN * TBUF_ALIGN);
}

static int slot_size_ok(size_t slot_size) {
    return slot_size > 0 && slot_size <= (UINT32_MAX - TBUF_ALIGN) / 3;
}

static unsigned char *slot_ptr(const struct tbuf *tb, uint32_t idx) {
    return (unsigned char *)tb + TBUF_ALIGN + (size_t)idx * tb->stride;
}

size_t tbuf_bytes(size_t slot_size) {
    if (!slot_size_ok(slot_size))
        return 0;
    return TBUF_ALIGN + 3 * (size_t)stride_for(slot_size);
}

int tbuf_init(struct tbuf *tb, size_t slot_size, uint32_t gen,
              struct tbuf_consumer *c) {
    if (!tb || !c || !slot_size_ok(slot_size) || gen > TBUF_GEN_MAX ||
        (uintptr_t)tb % TBUF_ALIGN != 0)
        return -EINVAL;

    tb->magic = 0;
    tb->version = TBUF_VERSION;
    tb->slot_size = (uint32_t)slot_size;
    tb->stride = stride_for(slot_size);
    memset(slot_ptr(tb, 0), 0, 3 * (size_t)tb->stride);

    /* front = 0, middle = 1 (not fresh), back = 2. */
    atomic_store_explicit(&tb->front_shadow, 0, memory_order_relaxed);
    atomic_store_explicit(&tb->state, (gen << TBUF_GEN_SHIFT) | 1u,
                          memory_order_relaxed);

    /* Magic last: an attacher that sees it also sees the fields above. */
    atomic_thread_fence(memory_order_release);
    tb->magic = TBUF_MAGIC;

    c->tb = tb;
    c->front = 0;
    c->gen = gen;
    return 0;
}

int tbuf_consumer_poll(struct tbuf_consumer *c) {
    struct tbuf *tb = c->tb;
    uint32_t s = atomic_load_explicit(&tb->state, memory_order_relaxed);
    if (!(s & TBUF_FRESH))
        return 0;

    /* Only the consumer clears FRESH, so it is still set here. */
    uint32_t old = atomic_exchange_explicit(
        &tb->state, (c->gen << TBUF_GEN_SHIFT) | c->front, memory_order_acq_rel);
    c->front = old & TBUF_IDX_MASK;
    atomic_store_explicit(&tb->front_shadow, c->front, memory_order_release);
    return 1;
}

const void *tbuf_consumer_slot(const struct tbuf_consumer *c) {
    return slot_ptr(c->tb, c->front);
}

int tbuf_producer_attach(struct tbuf_producer *p, struct tbuf *tb,
                         size_t slot_size) {
    if (!p || !tb || !slot_size_ok(slot_size) || (uintptr_t)tb % TBUF_ALIGN != 0)
        return -EINVAL;
    if (tb->magic != TBUF_MAGIC)
        return -EPROTO;
    atomic_thread_fence(memory_order_acquire);
    if (tb->version != TBUF_VERSION || tb->slot_size != slot_size ||
        tb->stride != stride_for(slot_size))
        return -EPROTO;

    uint32_t s = atomic_load_explicit(&tb->state, memory_order_acquire);
    uint32_t front = atomic_load_explicit(&tb->front_shadow, memory_order_acquire);
    uint32_t middle = s & TBUF_IDX_MASK;
    if (middle > 2 || front > 2)
        return -EPROTO;
    /* The consumer swaps state before updating front_shadow, so a stale
     * shadow always equals the new middle. */
    if (middle == front)
        return -EAGAIN;

    p->tb = tb;
    p->back = 3 - middle - front;
    p->gen = s >> TBUF_GEN_SHIFT;
    return 0;
}

void *tbuf_producer_slot(const struct tbuf_producer *p) {
    return slot_ptr(p->tb, p->back);
}

int tbuf_publish(struct tbuf_producer *p) {
    struct tbuf *tb = p->tb;
    uint32_t want = (p->gen << TBUF_GEN_SHIFT) | TBUF_FRESH | p->back;
    uint32_t s = atomic_load_explicit(&tb->state, memory_order_relaxed);

    /* CAS, not exchange, so a producer from a previous gen can't inject a
     * slot index into a re-initialised block. Retries only race a consumer
     * swap, which can't repeat until we publish again. */
    do {
        if ((s >> TBUF_GEN_SHIFT) != p->gen)
            return -ESTALE;
    } while (!atomic_compare_exchange_weak_explicit(
        &tb->state, &s, want, memory_order_acq_rel, memory_order_relaxed));

    p->back = s & TBUF_IDX_MASK;
    return 0;
}
