/* Sample-format conversion (src/common/pcmfmt.c, plan.md C16). */
#include "test.h"
#include "pcmfmt.h"

#include <stdint.h>
#include <string.h>

#define FRAMES 256u

static const enum pcm_fmt all_fmts[] = {PCM_FMT_S32_LE, PCM_FMT_S24_3LE, PCM_FMT_S24_LE};

static uint64_t g_rng = 0x9e3779b97f4a7c15u;

static uint32_t rnd32(void) {
    g_rng = g_rng * 6364136223846793005u + 1442695040888963407u;
    return (uint32_t)(g_rng >> 32);
}

static void put_le32(unsigned char *p, uint32_t w) {
    p[0] = (unsigned char)w;
    p[1] = (unsigned char)(w >> 8);
    p[2] = (unsigned char)(w >> 16);
    p[3] = (unsigned char)(w >> 24);
}

/* One capture frame with left word l and right word r, as raw bytes of f.
 * For S24_3LE only the low 3 bytes of each word are stored. */
static void put_frame(enum pcm_fmt f, unsigned char *p, uint32_t l, uint32_t r) {
    if (f == PCM_FMT_S24_3LE) {
        unsigned char tmp[4];
        put_le32(tmp, l);
        memcpy(p, tmp, 3);
        put_le32(tmp, r);
        memcpy(p + 3, tmp, 3);
    } else {
        put_le32(p, l);
        put_le32(p + 4, r);
    }
}

/* Capture of a single frame; returns L, and R via *r. */
static double capture1(enum pcm_fmt f, uint32_t l, uint32_t rw, double *r) {
    unsigned char raw[8];
    double src;
    float ring[2];
    put_frame(f, raw, l, rw);
    pcm_capture(f, raw, 1, &src, ring);
    TEST_EQ((double)ring[0], (double)(float)src);
    if (r)
        *r = ring[1];
    return src;
}

/* Playback of a single sample; returns the left channel's signed value, and
 * checks the right channel holds the same bytes. */
static int32_t playback1(enum pcm_fmt f, double x, size_t *bad) {
    unsigned char raw[8];
    size_t b = pcm_fmt_bytes(f);
    memset(raw, 0x5A, sizeof(raw));
    *bad = pcm_playback(f, &x, 1, raw);
    TEST_CHECK(memcmp(raw, raw + b, b) == 0);
    uint32_t w = (uint32_t)raw[0] | (uint32_t)raw[1] << 8 | (uint32_t)raw[2] << 16;
    if (b == 4)
        return (int32_t)(w | (uint32_t)raw[3] << 24);
    return ((int32_t)w ^ 0x800000) - 0x800000;
}

static void test_bytes(void) {
    TEST_EQ(pcm_fmt_bytes(PCM_FMT_S32_LE), 4u);
    TEST_EQ(pcm_fmt_bytes(PCM_FMT_S24_3LE), 3u);
    TEST_EQ(pcm_fmt_bytes(PCM_FMT_S24_LE), 4u);
    TEST_EQ(pcm_fmt_bytes((enum pcm_fmt)99), 0u);
}

/* 24-bit words decode as signed, and S24_LE ignores its top byte. */
static void test_sign_extension(void) {
    static const uint32_t tops[] = {0x00000000u, 0xFF000000u, 0xAB000000u};
    for (size_t t = 0; t < sizeof(tops) / sizeof(tops[0]); t++) {
        for (int k = 0; k < 2; k++) {
            enum pcm_fmt f = k ? PCM_FMT_S24_LE : PCM_FMT_S24_3LE;
            uint32_t top = tops[t];
            TEST_EQ(capture1(f, top | 0x7FFFFFu, 0, NULL), 1.0 - 0x1p-23);
            TEST_EQ(capture1(f, top | 0x800000u, 0, NULL), -1.0);
            TEST_EQ(capture1(f, top | 0xFFFFFFu, 0, NULL), -0x1p-23);
            TEST_EQ(capture1(f, top | 0x000001u, 0, NULL), 0x1p-23);
            TEST_EQ(capture1(f, top | 0x000000u, 0, NULL), 0.0);
        }
    }
}

static void test_full_scale(void) {
    TEST_EQ(capture1(PCM_FMT_S32_LE, 0x80000000u, 0, NULL), -1.0);
    TEST_EQ(capture1(PCM_FMT_S32_LE, 0x7FFFFFFFu, 0, NULL), 1.0 - 0x1p-31);
    TEST_EQ(capture1(PCM_FMT_S32_LE, 0x00000001u, 0, NULL), 0x1p-31);

    /* L and R land in the right places. */
    double r;
    for (size_t i = 0; i < 3; i++) {
        double l = capture1(all_fmts[i], 0x400000u, 0xC00000u, &r);
        if (all_fmts[i] == PCM_FMT_S32_LE) {
            TEST_EQ(l, 0x400000 * 0x1p-31);
            TEST_EQ(r, 0xC00000 * 0x1p-31);
        } else {
            TEST_EQ(l, 0.5);
            TEST_EQ(r, -0.5);
        }
    }
}

static void test_round_and_clamp(void) {
    size_t bad;
    for (size_t i = 0; i < 3; i++) {
        enum pcm_fmt f = all_fmts[i];
        int s32 = f == PCM_FMT_S32_LE;
        int32_t hi = s32 ? INT32_MAX : 0x7FFFFF;
        int32_t lo = s32 ? INT32_MIN : -0x800000;
        double lsb = s32 ? 0x1p-31 : 0x1p-23;

        TEST_EQ(playback1(f, 1.0, &bad), hi);
        TEST_EQ(playback1(f, 1.5, &bad), hi);
        TEST_EQ(playback1(f, 1e300, &bad), hi);
        TEST_EQ(playback1(f, -1.0, &bad), lo);
        TEST_EQ(playback1(f, -1.5, &bad), lo);
        TEST_EQ(playback1(f, -1e300, &bad), lo);
        TEST_EQ(bad, 0u);

        /* Just inside each rail. */
        TEST_EQ(playback1(f, 1.0 - lsb, &bad), hi);
        TEST_EQ(playback1(f, -1.0 + lsb, &bad), lo + 1);

        /* Round to nearest, ties to even. */
        TEST_EQ(playback1(f, 0.4 * lsb, &bad), 0);
        TEST_EQ(playback1(f, 0.6 * lsb, &bad), 1);
        TEST_EQ(playback1(f, 0.5 * lsb, &bad), 0);
        TEST_EQ(playback1(f, 1.5 * lsb, &bad), 2);
        TEST_EQ(playback1(f, -0.5 * lsb, &bad), 0);
        TEST_EQ(playback1(f, -1.5 * lsb, &bad), -2);
        TEST_EQ(bad, 0u);
    }
}

static void test_non_finite(void) {
    const double in[4] = {NAN, INFINITY, -INFINITY, 0.25};
    for (size_t i = 0; i < 3; i++) {
        enum pcm_fmt f = all_fmts[i];
        unsigned char raw[4 * 8];
        size_t b = pcm_fmt_bytes(f);
        memset(raw, 0x5A, sizeof(raw));
        TEST_EQ(pcm_playback(f, in, 4, raw), 3u);

        unsigned char zero[24] = {0};
        TEST_CHECK(memcmp(raw, zero, 3 * 2 * b) == 0);

        size_t bad;
        TEST_CHECK(memcmp(raw + 3 * 2 * b, raw + 3 * 2 * b + b, b) == 0);
        TEST_EQ(playback1(f, 0.25, &bad), f == PCM_FMT_S32_LE ? 0x20000000 : 0x200000);
    }
}

/* Exact bytes for S24_3LE and S24_LE playback. */
static void test_packing(void) {
    unsigned char raw[8];
    double x = 0x123456 * 0x1p-23;
    pcm_playback(PCM_FMT_S24_3LE, &x, 1, raw);
    static const unsigned char want3[6] = {0x56, 0x34, 0x12, 0x56, 0x34, 0x12};
    TEST_CHECK(memcmp(raw, want3, 6) == 0);

    x = -0x1p-23;
    pcm_playback(PCM_FMT_S24_3LE, &x, 1, raw);
    static const unsigned char wantm[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_CHECK(memcmp(raw, wantm, 6) == 0);

    /* S24_LE writes the sign extension in the top byte. */
    pcm_playback(PCM_FMT_S24_LE, &x, 1, raw);
    static const unsigned char want4[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_CHECK(memcmp(raw, want4, 8) == 0);
    x = 0x123456 * 0x1p-23;
    pcm_playback(PCM_FMT_S24_LE, &x, 1, raw);
    static const unsigned char wantp[8] = {0x56, 0x34, 0x12, 0x00, 0x56, 0x34, 0x12, 0x00};
    TEST_CHECK(memcmp(raw, wantp, 8) == 0);
}

/* Random L/R through capture, then L back through playback: bit-exact, and
 * the ring holds both channels. */
static void test_round_trip(void) {
    static unsigned char in[FRAMES * 8], out[FRAMES * 8];
    static double src[FRAMES];
    static float ring[2 * FRAMES];
    static uint32_t lw[FRAMES], rw[FRAMES];

    for (size_t i = 0; i < 3; i++) {
        enum pcm_fmt f = all_fmts[i];
        size_t b = pcm_fmt_bytes(f);
        int s32 = f == PCM_FMT_S32_LE;

        for (size_t j = 0; j < FRAMES; j++) {
            lw[j] = rnd32();
            rw[j] = rnd32();
            if (!s32) {
                /* Sign-extended, so S24_LE's top byte compares exactly. */
                lw[j] = (uint32_t)(((int32_t)(lw[j] & 0xFFFFFFu) ^ 0x800000) - 0x800000);
                rw[j] = (uint32_t)(((int32_t)(rw[j] & 0xFFFFFFu) ^ 0x800000) - 0x800000);
            }
            put_frame(f, in + j * 2 * b, lw[j], rw[j]);
        }
        lw[0] = s32 ? 0x80000000u : 0xFF800000u; /* most negative */
        lw[1] = s32 ? 0x7FFFFFFFu : 0x007FFFFFu; /* most positive */
        put_frame(f, in, lw[0], rw[0]);
        put_frame(f, in + 2 * b, lw[1], rw[1]);

        pcm_capture(f, in, FRAMES, src, ring);
        TEST_EQ(pcm_playback(f, src, FRAMES, out), 0u);

        double scale = s32 ? 0x1p-31 : 0x1p-23;
        for (size_t j = 0; j < FRAMES; j++) {
            TEST_CHECK(memcmp(out + j * 2 * b, in + j * 2 * b, b) == 0);
            TEST_CHECK(memcmp(out + j * 2 * b + b, in + j * 2 * b, b) == 0);
            TEST_EQ((double)ring[2 * j], (double)(float)((int32_t)lw[j] * scale));
            TEST_EQ((double)ring[2 * j + 1], (double)(float)((int32_t)rw[j] * scale));
        }
    }
}

int main(void) {
    test_bytes();
    test_sign_extension();
    test_full_scale();
    test_round_and_clamp();
    test_non_finite();
    test_packing();
    test_round_trip();
    return test_failures();
}
