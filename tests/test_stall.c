/* RT-thread stall decision (src/common/stall.c, plan.md C14). */
#include "test.h"
#include "stall.h"

#include <stdint.h>

#define MS       1000000ull
#define PERIOD   500000ull /* one 48-frame period at 96 kHz */
#define CHECK    (10 * MS) /* watchdog-thread cadence */
#define T0       (1000 * MS)

/* The heartbeat a healthy RT thread shows at time t, ticking every period
 * from t = 0. */
static uint64_t hb_at(uint64_t t) {
    return t / PERIOD;
}

/* Before the first change it never stalls, however long the wait. */
static void test_unarmed(void) {
    struct stall s;
    uint64_t age = 123;
    stall_init(&s);
    for (uint64_t t = T0; t <= T0 + 10000 * MS; t += CHECK) {
        TEST_EQ(stall_check(&s, 7, t, &age), 0);
        TEST_EQ(age, 0u);
    }
    TEST_EQ(s.armed, 0);
}

static void test_steady_progress(void) {
    struct stall s;
    uint64_t age;
    stall_init(&s);
    for (uint64_t t = T0; t <= T0 + 60000 * MS; t += CHECK) {
        TEST_EQ(stall_check(&s, hb_at(t), t, &age), 0);
        TEST_EQ(age, 0u);
    }
}

/* Arms on a change, then trips at exactly STALL_MS of no change, and stays. */
static void test_threshold(void) {
    struct stall s;
    uint64_t age;
    stall_init(&s);
    TEST_EQ(stall_check(&s, 41, T0, &age), 0);
    TEST_EQ(stall_check(&s, 42, T0 + CHECK, &age), 0);
    TEST_EQ(s.armed, 1);

    uint64_t seen = T0 + CHECK;
    TEST_EQ(stall_check(&s, 42, seen + 50 * MS, &age), 0);
    TEST_EQ(age, 50 * MS);
    TEST_EQ(stall_check(&s, 42, seen + STALL_MS * MS - 1, &age), 0);
    TEST_EQ(age, STALL_MS * MS - 1);
    TEST_EQ(stall_check(&s, 42, seen + STALL_MS * MS, &age), 1);
    TEST_EQ(age, STALL_MS * MS);
    TEST_EQ(stall_check(&s, 42, seen + 5000 * MS, &age), 1);
    TEST_EQ(age, 5000 * MS);
}

/* The age runs from the last observed change, not from the baseline. */
static void test_progress_resets(void) {
    struct stall s;
    uint64_t age;
    stall_init(&s);
    TEST_EQ(stall_check(&s, 1, T0, &age), 0);
    TEST_EQ(stall_check(&s, 2, T0 + CHECK, &age), 0);
    TEST_EQ(stall_check(&s, 2, T0 + CHECK + 90 * MS, &age), 0);
    TEST_EQ(stall_check(&s, 3, T0 + CHECK + 95 * MS, &age), 0);
    TEST_EQ(stall_check(&s, 3, T0 + CHECK + 95 * MS + 99 * MS, &age), 0);
    TEST_EQ(age, 99 * MS);
    TEST_EQ(stall_check(&s, 3, T0 + CHECK + 95 * MS + 100 * MS, &age), 1);
}

static void test_wraparound(void) {
    struct stall s;
    uint64_t age;
    stall_init(&s);
    TEST_EQ(stall_check(&s, UINT64_MAX - 1, T0, &age), 0);
    TEST_EQ(stall_check(&s, UINT64_MAX, T0 + CHECK, &age), 0);
    TEST_EQ(stall_check(&s, 0, T0 + CHECK + 90 * MS, &age), 0);
    TEST_EQ(stall_check(&s, 0, T0 + CHECK + 90 * MS + 99 * MS, &age), 0);
    TEST_EQ(age, 99 * MS);
}

/* A watchdog thread delayed 300 ms between checks, while the RT thread
 * keeps going, never sees a stall. */
static void test_delayed_watchdog(void) {
    struct stall s;
    uint64_t age;
    stall_init(&s);
    for (uint64_t t = T0; t <= T0 + 30000 * MS; t += 300 * MS)
        TEST_EQ(stall_check(&s, hb_at(t), t, &age), 0);
}

/* A real stall is caught within STALL_MS plus one check interval of the
 * last heartbeat. */
static void test_detection_latency(void) {
    struct stall s;
    uint64_t age;
    stall_init(&s);
    /* The RT thread stops between two checks; its last heartbeat is the
     * last period boundary before then. */
    uint64_t stop = T0 + 5003 * MS + 123456;
    uint64_t last_beat = stop / PERIOD * PERIOD;
    uint64_t t = T0;
    while (stall_check(&s, hb_at(t < stop ? t : stop), t, &age) == 0)
        t += CHECK;
    TEST_CHECK(t >= last_beat + STALL_MS * MS); /* never early */
    TEST_CHECK(t <= last_beat + STALL_MS * MS + CHECK);
}

static void test_edges(void) {
    struct stall s;
    uint64_t age = 99;
    stall_init(&s);
    TEST_EQ(stall_check(&s, 1, T0, NULL), 0);
    TEST_EQ(stall_check(&s, 2, T0 + CHECK, NULL), 0);
    TEST_EQ(stall_check(&s, 2, T0 + CHECK + 200 * MS, NULL), 1);

    /* Time going backwards gives age 0, not a huge unsigned age. */
    stall_init(&s);
    TEST_EQ(stall_check(&s, 1, T0, &age), 0);
    TEST_EQ(stall_check(&s, 2, T0 + CHECK, &age), 0);
    TEST_EQ(stall_check(&s, 2, T0, &age), 0);
    TEST_EQ(age, 0u);
}

int main(void) {
    test_unarmed();
    test_steady_progress();
    test_threshold();
    test_progress_resets();
    test_wraparound();
    test_delayed_watchdog();
    test_detection_latency();
    test_edges();
    return test_failures();
}
