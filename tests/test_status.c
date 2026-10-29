/* Status block layout and meter windows (src/common/status.c, issues.md C10). */
#include "test.h"
#include "seqlock.h"
#include "status.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#define RATE    96000u
#define P48     48u
#define PERIODS 100u /* periods per METER_WINDOW_MS at RATE / P48 */

static float g_ring[2 * P48];
static double g_out[P48];

static _Alignas(SEQLOCK_ALIGN) unsigned char g_buf[256];

static void silence(void) {
    memset(g_ring, 0, sizeof g_ring);
    memset(g_out, 0, sizeof g_out);
}

/* Feeds n silent periods at gain 1; returns how many closed a window. */
static unsigned feed(struct status_meter *m, unsigned n, struct status_block *st) {
    unsigned closed = 0;
    silence();
    for (unsigned i = 0; i < n; i++)
        closed += (unsigned)status_meter_period(m, g_ring, g_out, P48, 1.0, st);
    return closed;
}

static void test_layout(void) {
    TEST_EQ(sizeof(struct status_block), 80u);
    TEST_EQ(offsetof(struct status_block, heartbeat), 0u);
    TEST_EQ(offsetof(struct status_block, xruns), 8u);
    TEST_EQ(offsetof(struct status_block, rejected_coeff), 16u);
    TEST_EQ(offsetof(struct status_block, rejected_param), 24u);
    TEST_EQ(offsetof(struct status_block, meter_window), 32u);
    TEST_EQ(offsetof(struct status_block, peak_source), 40u);
    TEST_EQ(offsetof(struct status_block, peak_system), 48u);
    TEST_EQ(offsetof(struct status_block, peak_out), 56u);
    TEST_EQ(offsetof(struct status_block, gain_min), 64u);
    TEST_EQ(offsetof(struct status_block, mute_state), 72u);
    TEST_EQ(offsetof(struct status_block, mute_reason), 76u);
    TEST_CHECK(seqlock_bytes(sizeof(struct status_block)) != 0);
    TEST_CHECK(seqlock_bytes(sizeof(struct status_block)) <= sizeof g_buf);

    /* seqlock_init zeroes the data: that must read as "nothing yet". */
    struct status_block st;
    memset(&st, 0, sizeof st);
    TEST_EQ(st.mute_state, (uint32_t)MUTE_MUTED);
    TEST_EQ(st.heartbeat, 0u);
    TEST_EQ(st.meter_window, 0u);
}

static void test_init(void) {
    struct status_meter m;
    TEST_EQ(status_meter_init(NULL, RATE), -EINVAL);
    TEST_EQ(status_meter_init(&m, 0), -EINVAL);
    TEST_EQ(status_meter_init(&m, 19), -EINVAL);
    TEST_EQ(status_meter_init(&m, 20), 0);
    TEST_EQ(m.window_frames, 1u);
    TEST_EQ(status_meter_init(&m, RATE), 0);
    TEST_EQ(m.window_frames, PERIODS * P48);
}

static void test_window_close(void) {
    struct status_meter m;
    struct status_block st, before;
    TEST_EQ(status_meter_init(&m, RATE), 0);

    /* 99 periods: no window yet, st untouched. */
    memset(&st, 0x5a, sizeof st);
    before = st;
    TEST_EQ(feed(&m, PERIODS - 1, &st), 0u);
    TEST_CHECK(memcmp(&st, &before, sizeof st) == 0);

    /* The 100th closes window 1; the next 100 close exactly window 2. */
    TEST_EQ(feed(&m, 1, &st), 1u);
    TEST_EQ(st.meter_window, 1u);
    TEST_EQ(st.heartbeat, before.heartbeat); /* only meter fields written */
    TEST_EQ(st.mute_reason, before.mute_reason);
    TEST_EQ(feed(&m, PERIODS - 1, &st), 0u);
    TEST_EQ(st.meter_window, 1u);
    TEST_EQ(feed(&m, 1, &st), 1u);
    TEST_EQ(st.meter_window, 2u);
    TEST_EQ(feed(&m, 10 * PERIODS, &st), 10u);
    TEST_EQ(st.meter_window, 12u);

    /* Silence at gain 1 meters as zero peaks and no reduction. */
    TEST_EQ(st.peak_source, 0.0);
    TEST_EQ(st.peak_system, 0.0);
    TEST_EQ(st.peak_out, 0.0);
    TEST_EQ(st.gain_min, 1.0);
}

static void test_peaks(void) {
    struct status_meter m;
    struct status_block st;
    TEST_EQ(status_meter_init(&m, RATE), 0);
    memset(&st, 0, sizeof st);

    /* Negative spikes in each signal, in different periods of one window. */
    TEST_EQ(feed(&m, 10, &st), 0u);
    g_ring[2 * 7] = -0.5f;
    TEST_EQ(status_meter_period(&m, g_ring, g_out, P48, 1.0, &st), 0);
    silence();
    g_ring[2 * 3 + 1] = -0.25f;
    TEST_EQ(status_meter_period(&m, g_ring, g_out, P48, 1.0, &st), 0);
    silence();
    g_out[P48 - 1] = -0.125;
    TEST_EQ(status_meter_period(&m, g_ring, g_out, P48, 1.0, &st), 0);
    TEST_EQ(feed(&m, PERIODS - 13, &st), 1u);

    TEST_EQ(st.peak_source, 0.5);
    TEST_EQ(st.peak_system, 0.25);
    TEST_EQ(st.peak_out, 0.125);
    TEST_EQ(st.gain_min, 1.0);
}

static void test_two_windows(void) {
    struct status_meter m;
    struct status_block st;
    TEST_EQ(status_meter_init(&m, RATE), 0);
    memset(&st, 0, sizeof st);

    /* A spike in the last period of window 1. */
    TEST_EQ(feed(&m, PERIODS - 1, &st), 0u);
    g_ring[0] = 0.75f;
    TEST_EQ(status_meter_period(&m, g_ring, g_out, P48, 1.0, &st), 1);
    TEST_EQ(st.meter_window, 1u);
    TEST_EQ(st.peak_source, 0.75);

    /* Still covered by window 2's publish, gone at window 3. */
    TEST_EQ(feed(&m, PERIODS, &st), 1u);
    TEST_EQ(st.meter_window, 2u);
    TEST_EQ(st.peak_source, 0.75);
    TEST_EQ(feed(&m, PERIODS, &st), 1u);
    TEST_EQ(st.meter_window, 3u);
    TEST_EQ(st.peak_source, 0.0);
}

static void test_gain(void) {
    struct status_meter m;
    struct status_block st;
    TEST_EQ(status_meter_init(&m, RATE), 0);
    memset(&st, 0, sizeof st);
    silence();

    /* 0.5 in window 1, 0.25 in window 2: the lowest of both is published. */
    TEST_EQ(status_meter_period(&m, g_ring, g_out, P48, 0.5, &st), 0);
    TEST_EQ(feed(&m, PERIODS - 1, &st), 1u);
    TEST_EQ(st.gain_min, 0.5);
    TEST_EQ(status_meter_period(&m, g_ring, g_out, P48, 0.25, &st), 0);
    TEST_EQ(feed(&m, PERIODS - 1, &st), 1u);
    TEST_EQ(st.gain_min, 0.25);
    TEST_EQ(feed(&m, PERIODS, &st), 1u);
    TEST_EQ(st.gain_min, 0.25);
    TEST_EQ(feed(&m, PERIODS, &st), 1u);
    TEST_EQ(st.gain_min, 1.0);
}

static void test_seqlock_roundtrip(void) {
    struct seqlock *sl = (struct seqlock *)g_buf;
    struct seqlock_writer w;
    struct seqlock_reader rd;
    struct status_block st, got;

    memset(g_buf, 0, sizeof g_buf);
    TEST_EQ(seqlock_init(sl, sizeof st, 7, &w), 0);
    TEST_EQ(seqlock_reader_attach(&rd, sl, sizeof st), 0);

    /* Before the first write, the reader sees the zeroed block. */
    memset(&got, 0xff, sizeof got);
    TEST_EQ(seqlock_read(&rd, &got), 0);
    TEST_EQ(got.heartbeat, 0u);
    TEST_EQ(got.meter_window, 0u);
    TEST_EQ(got.mute_state, (uint32_t)MUTE_MUTED);

    st.heartbeat = 123456789012u;
    st.xruns = 3;
    st.rejected_coeff = 4;
    st.rejected_param = 5;
    st.meter_window = 6;
    st.peak_source = 0.5;
    st.peak_system = 0.25;
    st.peak_out = 0.125;
    st.gain_min = 0.75;
    st.mute_state = MUTE_FADING_IN;
    st.mute_reason = MUTE_WAIT_SELFCHECK | MUTE_WAIT_PARAMS;
    seqlock_write(&w, &st);

    memset(&got, 0, sizeof got);
    TEST_EQ(seqlock_read(&rd, &got), 0);
    TEST_CHECK(memcmp(&got, &st, sizeof st) == 0);
}

int main(void) {
    test_layout();
    test_init();
    test_window_close();
    test_peaks();
    test_two_windows();
    test_gain();
    test_seqlock_roundtrip();
    return test_failures();
}
