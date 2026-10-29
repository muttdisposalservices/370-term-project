#define _POSIX_C_SOURCE 200809L

#include "shm.h"
#include "tbuf.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(sizeof(off_t) == 8, "shm needs a 64-bit off_t");

/* glibc's shm_open adds O_CLOEXEC and O_NOFOLLOW itself. */

static int bytes_ok(size_t bytes) {
    return bytes > 0 && (uint64_t)bytes <= (uint64_t)INT64_MAX;
}

/* Maps fd and closes it. On failure the fd is still closed. */
static int map_fd(struct shm_seg *s, int fd, const struct stat *st,
                  const char *name, size_t bytes, int prot) {
    int ret;

    s->base = mmap(NULL, bytes, prot, MAP_SHARED, fd, 0);
    if (s->base == MAP_FAILED) {
        ret = -errno;
        s->base = NULL;
        (void)close(fd);
        return ret;
    }
    s->name = name;
    s->bytes = bytes;
    s->dev = st->st_dev;
    s->ino = st->st_ino;

    if (close(fd) != 0) {
        ret = -errno;
        (void)shm_seg_close(s);
        return ret;
    }
    return 0;
}

int shm_seg_create(struct shm_seg *s, const char *name, size_t bytes, mode_t mode) {
    struct stat st;
    int fd, created = 0, ret;

    if (!s || !name || !bytes_ok(bytes))
        return -EINVAL;

    fd = shm_open(name, O_RDWR, 0);
    if (fd >= 0) {
        if (fstat(fd, &st) != 0)
            goto fail;
        if (S_ISREG(st.st_mode) && (uint64_t)st.st_size == bytes)
            goto map;
        if (close(fd) != 0)
            return -errno;
    } else if (errno != ENOENT && errno != EACCES) {
        return -errno;
    }

    /* Unusable or missing: replace it. Old mappings keep the old object. */
    if (shm_unlink(name) != 0 && errno != ENOENT)
        return -errno;
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, mode);
    if (fd < 0)
        return -errno;
    created = 1;
    if (fchmod(fd, mode) != 0 || ftruncate(fd, (off_t)bytes) != 0 ||
        fstat(fd, &st) != 0)
        goto fail;

map:
    ret = map_fd(s, fd, &st, name, bytes, PROT_READ | PROT_WRITE);
    if (ret != 0)
        goto fail_closed;
    return created;

fail:
    ret = -errno;
    (void)close(fd);
fail_closed:
    if (created)
        (void)shm_unlink(name); /* don't leave a half-made segment */
    return ret;
}

int shm_seg_attach(struct shm_seg *s, const char *name, size_t bytes, int writable) {
    struct stat st;
    int fd, ret;

    if (!s || !name || !bytes_ok(bytes))
        return -EINVAL;

    fd = shm_open(name, writable ? O_RDWR : O_RDONLY, 0);
    if (fd < 0)
        return -errno;
    if (fstat(fd, &st) != 0) {
        ret = -errno;
        goto fail;
    }
    /* Checked before mmap: a short file would SIGBUS on first touch. */
    if (!S_ISREG(st.st_mode) || (uint64_t)st.st_size != bytes) {
        ret = -EPROTO;
        goto fail;
    }
    return map_fd(s, fd, &st, name, bytes,
                  writable ? PROT_READ | PROT_WRITE : PROT_READ);

fail:
    (void)close(fd);
    return ret;
}

int shm_seg_replaced(const struct shm_seg *s) {
    struct stat st;
    int fd, ret;

    if (!s || !s->name)
        return -EINVAL;

    fd = shm_open(s->name, O_RDONLY, 0);
    if (fd < 0)
        return errno == ENOENT ? 1 : -errno;
    if (fstat(fd, &st) != 0) {
        ret = -errno;
        (void)close(fd);
        return ret;
    }
    if (close(fd) != 0)
        return -errno;
    return st.st_dev != s->dev || st.st_ino != s->ino;
}

int shm_seg_close(struct shm_seg *s) {
    if (!s || !s->base)
        return 0;
    int ret = munmap(s->base, s->bytes) != 0 ? -errno : 0;
    s->base = NULL;
    return ret;
}

uint32_t shm_next_gen(int have_prev, uint32_t prev, uint64_t seed) {
    return (have_prev ? prev + 1u : (uint32_t)seed) & TBUF_GEN_MAX;
}
