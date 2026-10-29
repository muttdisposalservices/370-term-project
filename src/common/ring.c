#include "ring.h"

#include <errno.h>
#include <string.h>

_Static_assert(sizeof(struct ring) <= RING_ALIGN, "ring header exceeds one line");

static int shape_ok(uint32_t cap, uint32_t frame_size) {
    return cap > 0 && (cap & (cap - 1)) == 0 && frame_size > 0 &&
           (size_t)cap <= (SIZE_MAX - RING_ALIGN) / frame_size;
}

static unsigned char *frames_of(const struct ring *r) {
    return (unsigned char *)r + RING_ALIGN;
}

/* Frames [at, at+n) occupy [off, off+first) and then [0, n-first). */
static uint32_t split(uint64_t at, uint32_t n, uint32_t cap, uint32_t *first) {
    uint32_t off = (uint32_t)(at & (cap - 1));
    *first = cap - off < n ? cap - off : n;
    return off;
}

size_t ring_bytes(uint32_t cap_frames, uint32_t frame_size) {
    if (!shape_ok(cap_frames, frame_size))
        return 0;
    return RING_ALIGN + (size_t)cap_frames * frame_size;
}

int ring_init(struct ring *r, uint32_t cap_frames, uint32_t frame_size,
              uint32_t gen, struct ring_writer *w) {
    if (!r || !w || !shape_ok(cap_frames, frame_size) ||
        (uintptr_t)r % RING_ALIGN != 0)
        return -EINVAL;

    w->r = r;
    w->cap = cap_frames;
    w->frame_size = frame_size;

    if (r->magic == RING_MAGIC) {
        atomic_thread_fence(memory_order_acquire);
        uint64_t commit = atomic_load_explicit(&r->commit, memory_order_relaxed);
        uint64_t claim = atomic_load_explicit(&r->claim, memory_order_relaxed);
        if (r->version == RING_VERSION && r->cap == cap_frames &&
            r->frame_size == frame_size && commit <= claim &&
            claim - commit <= cap_frames) {
            /* Resume. [commit, claim) is a crashed writer's partial push that
             * no reader has accepted: overwrite it with silence. */
            uint32_t n = (uint32_t)(claim - commit), first;
            uint32_t off = split(commit, n, cap_frames, &first);
            memset(frames_of(r) + (size_t)off * frame_size, 0,
                   (size_t)first * frame_size);
            memset(frames_of(r), 0, (size_t)(n - first) * frame_size);

            /* gen before commit: a reader that sees the new commit sees the
             * new gen. */
            atomic_store_explicit(&r->gen, gen, memory_order_relaxed);
            atomic_store_explicit(&r->commit, claim, memory_order_release);
            w->head = claim;
            return 1;
        }
    }

    r->magic = 0;
    r->version = RING_VERSION;
    r->frame_size = frame_size;
    r->cap = cap_frames;
    atomic_store_explicit(&r->gen, gen, memory_order_relaxed);
    atomic_store_explicit(&r->claim, 0, memory_order_relaxed);
    atomic_store_explicit(&r->commit, 0, memory_order_relaxed);

    /* Magic last: an attacher that sees it also sees the fields above. */
    atomic_thread_fence(memory_order_release);
    r->magic = RING_MAGIC;

    w->head = 0;
    return 0;
}

int ring_push(struct ring_writer *w, const void *frames, uint32_t n) {
    if (n > w->cap)
        return -EINVAL;

    struct ring *r = w->r;
    uint64_t end = w->head + n;
    uint32_t first;
    uint32_t off = split(w->head, n, w->cap, &first);
    size_t fs = w->frame_size;

    /* Claim before the data: a reader that copied any of the new frames
     * then sees a claim that covers them. */
    atomic_store_explicit(&r->claim, end, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);

    memcpy(frames_of(r) + off * fs, frames, first * fs);
    memcpy(frames_of(r), (const unsigned char *)frames + first * fs,
           (n - first) * fs);

    atomic_store_explicit(&r->commit, end, memory_order_release);
    w->head = end;
    return 0;
}

int ring_reader_attach(struct ring_reader *rd, struct ring *r,
                       uint32_t cap_frames, uint32_t frame_size) {
    if (!rd || !r || !shape_ok(cap_frames, frame_size) ||
        (uintptr_t)r % RING_ALIGN != 0)
        return -EINVAL;
    if (r->magic != RING_MAGIC)
        return -EPROTO;
    atomic_thread_fence(memory_order_acquire);
    if (r->version != RING_VERSION || r->cap != cap_frames ||
        r->frame_size != frame_size)
        return -EPROTO;

    /* Sizes are cached so a corrupted header can't steer a later copy. */
    rd->r = r;
    rd->cap = cap_frames;
    rd->frame_size = frame_size;
    rd->dropped = 0;
    rd->gen = atomic_load_explicit(&r->gen, memory_order_acquire);
    rd->pos = atomic_load_explicit(&r->commit, memory_order_acquire);
    return 0;
}

int ring_read(struct ring_reader *rd, void *dst, uint32_t n) {
    if (n == 0 || n > rd->cap)
        return -EINVAL;

    struct ring *r = rd->r;
    uint64_t commit = atomic_load_explicit(&r->commit, memory_order_acquire);
    if (atomic_load_explicit(&r->gen, memory_order_relaxed) != rd->gen)
        goto stale;
    if (commit - rd->pos > rd->cap)
        goto lapped;
    if (commit - rd->pos < n)
        return -EAGAIN;

    uint32_t first;
    uint32_t off = split(rd->pos, n, rd->cap, &first);
    size_t fs = rd->frame_size;
    memcpy(dst, frames_of(r) + off * fs, first * fs);
    memcpy((unsigned char *)dst + first * fs, frames_of(r), (n - first) * fs);

    /* Copy before claim. Frame pos is overwritten once claim > pos + cap,
     * so this also catches a writer still mid-push over our frames. */
    atomic_thread_fence(memory_order_acquire);
    uint64_t claim = atomic_load_explicit(&r->claim, memory_order_relaxed);
    if (atomic_load_explicit(&r->gen, memory_order_relaxed) != rd->gen)
        goto stale;
    if (claim - rd->pos > rd->cap)
        goto lapped;

    rd->pos += n;
    return 0;

lapped:
    commit = atomic_load_explicit(&r->commit, memory_order_acquire);
    if (commit > rd->pos)
        rd->dropped += commit - rd->pos;
    rd->pos = commit;
    return -EPIPE;

stale:
    rd->gen = atomic_load_explicit(&r->gen, memory_order_acquire);
    rd->pos = atomic_load_explicit(&r->commit, memory_order_acquire);
    return -ESTALE;
}
