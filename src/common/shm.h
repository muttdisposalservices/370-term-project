/* POSIX shm segment lifecycle (resolved_issues.md C12). audiod is the only
 * creator: shm_seg_create maps a segment at exactly the expected size and,
 * if what is there can't be used (wrong size, not a regular file, can't be
 * opened read-write), unlinks it and creates a fresh one. A bad header
 * inside a usable segment is not handled here: ring_init, tbuf_init and
 * seqlock_init re-initialise it in place.
 *
 * analyzerd and ctld only attach. A recreated segment is a new object, so a
 * reader still mapping the old one sees no gen change; while waiting or
 * stalled it calls shm_seg_replaced and attaches again by name.
 *
 * Not RT-safe: every call makes syscalls. The fd is closed once mapped, so
 * nothing is left open across a restart. Segments are never unlinked on
 * exit: they outlive any one process (ProposedStructure.md §5). */
#ifndef SHM_H
#define SHM_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define SHM_RING_NAME   "/dspunit-ring"
#define SHM_COEFF_NAME  "/dspunit-coeff"
#define SHM_PARAM_NAME  "/dspunit-param"
#define SHM_STATUS_NAME "/dspunit-status"

/* name is not copied: it must outlive the seg. */
struct shm_seg {
    const char *name;
    void *base;
    size_t bytes;
    dev_t dev;
    ino_t ino;
};

/* audiod. Maps name read-write at exactly bytes, creating it with mode (not
 * masked by umask) if needed. Returns 0 (existing segment kept), 1 (created
 * or recreated; contents are zero) or -errno. -EACCES/-EPERM mean an
 * unusable segment couldn't be unlinked: a config error. */
int shm_seg_create(struct shm_seg *s, const char *name, size_t bytes, mode_t mode);

/* analyzerd, ctld. Never creates. Maps read-only unless writable. Returns
 * 0, -ENOENT (not created yet), -EPROTO (wrong size or not a regular file;
 * audiod will replace it) or another -errno. */
int shm_seg_attach(struct shm_seg *s, const char *name, size_t bytes, int writable);

/* 1 if name now names a different object or none, 0 if still s's, -errno. */
int shm_seg_replaced(const struct shm_seg *s);

/* Unmaps. Safe on a zeroed or already-closed seg. Never unlinks. Returns 0
 * or -errno. */
int shm_seg_close(struct shm_seg *s);

/* The gen audiod passes to every init: prev + 1 if the old status block was
 * valid, else seed. Masked to tbuf's 24-bit field. */
uint32_t shm_next_gen(int have_prev, uint32_t prev, uint64_t seed);

#endif
