#include "pcmfmt.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* S32_LE and S24_LE words are copied with memcpy, which is only correct on a
 * little-endian host (the Pi and the x86 test machines both are). */
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "pcmfmt assumes a little-endian host");

#define S32_SCALE 0x1p-31
#define S24_SCALE 0x1p-23
#define S32_MAX   2147483647.0
#define S32_MIN   (-2147483648.0)
#define S24_MAX   8388607.0
#define S24_MIN   (-8388608.0)

/* Low 24 bits of w as a signed value, without relying on an
 * implementation-defined right shift of a negative number. */
static inline int32_t sext24(uint32_t w) {
    return ((int32_t)(w & 0xFFFFFFu) ^ 0x800000) - 0x800000;
}

static inline int32_t load_s32(const unsigned char *p) {
    int32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static inline int32_t load_s24_3(const unsigned char *p) {
    return sext24((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16);
}

static inline int32_t load_s24_4(const unsigned char *p) {
    uint32_t w;
    memcpy(&w, p, sizeof(w));
    return sext24(w);
}

/* x scaled by up, rounded and clamped to [lo, hi]; 0 if x is non-finite.
 * The clamp happens in the double domain, since converting an out-of-range
 * double to an integer is undefined. */
static inline int32_t quantize(double x, double up, double lo, double hi, size_t *bad) {
    if (!isfinite(x)) {
        (*bad)++;
        return 0;
    }
    double s = x * up;
    if (s >= hi)
        return (int32_t)hi;
    if (s <= lo)
        return (int32_t)lo;
    return (int32_t)lrint(s);
}

size_t pcm_fmt_bytes(enum pcm_fmt f) {
    switch (f) {
    case PCM_FMT_S32_LE:
        return 4;
    case PCM_FMT_S24_3LE:
        return 3;
    case PCM_FMT_S24_LE:
        return 4;
    }
    return 0;
}

void pcm_capture(enum pcm_fmt f, const void *raw, size_t frames,
                 double *source, float *ring) {
    const unsigned char *p = raw;

    switch (f) {
    case PCM_FMT_S32_LE:
        for (size_t i = 0; i < frames; i++, p += 8) {
            double l = load_s32(p) * S32_SCALE;
            double r = load_s32(p + 4) * S32_SCALE;
            source[i] = l;
            ring[2 * i] = (float)l;
            ring[2 * i + 1] = (float)r;
        }
        break;
    case PCM_FMT_S24_3LE:
        for (size_t i = 0; i < frames; i++, p += 6) {
            double l = load_s24_3(p) * S24_SCALE;
            double r = load_s24_3(p + 3) * S24_SCALE;
            source[i] = l;
            ring[2 * i] = (float)l;
            ring[2 * i + 1] = (float)r;
        }
        break;
    case PCM_FMT_S24_LE:
        for (size_t i = 0; i < frames; i++, p += 8) {
            double l = load_s24_4(p) * S24_SCALE;
            double r = load_s24_4(p + 4) * S24_SCALE;
            source[i] = l;
            ring[2 * i] = (float)l;
            ring[2 * i + 1] = (float)r;
        }
        break;
    }
}

size_t pcm_playback(enum pcm_fmt f, const double *mono, size_t frames, void *raw) {
    unsigned char *p = raw;
    size_t bad = 0;

    switch (f) {
    case PCM_FMT_S32_LE:
        for (size_t i = 0; i < frames; i++, p += 8) {
            int32_t v = quantize(mono[i], 0x1p31, S32_MIN, S32_MAX, &bad);
            memcpy(p, &v, 4);
            memcpy(p + 4, &v, 4);
        }
        break;
    case PCM_FMT_S24_3LE:
        for (size_t i = 0; i < frames; i++, p += 6) {
            uint32_t u = (uint32_t)quantize(mono[i], 0x1p23, S24_MIN, S24_MAX, &bad);
            p[0] = p[3] = (unsigned char)u;
            p[1] = p[4] = (unsigned char)(u >> 8);
            p[2] = p[5] = (unsigned char)(u >> 16);
        }
        break;
    case PCM_FMT_S24_LE:
        /* The top byte is written as the sign extension. */
        for (size_t i = 0; i < frames; i++, p += 8) {
            int32_t v = quantize(mono[i], 0x1p23, S24_MIN, S24_MAX, &bad);
            memcpy(p, &v, 4);
            memcpy(p + 4, &v, 4);
        }
        break;
    }
    return bad;
}
