/* RT-thread stall decision for audiod's watchdog thread (plan.md C14).
 *
 * The watchdog thread reads the status block's heartbeat every few ms and
 * passes it here with CLOCK_MONOTONIC. If the heartbeat hasn't changed for
 * STALL_MS since a change was last seen, the RT thread is stalled: the
 * caller logs the heartbeat and age, then _exit(12).
 *
 * Armed on the first observed change, not the first call, so a slow but
 * healthy startup (PCM open, hw_params, the C4 capture settle) can't trip
 * it. A hang before the first heartbeat is covered by READY=1 never being
 * sent (TimeoutStartSec). After that, the RT thread must keep advancing the
 * heartbeat through every relock and settle loop.
 *
 * The age runs from when the change was observed, which is never before the
 * true last heartbeat, so a delayed watchdog thread can only detect a stall
 * late, never report a false one. If a status read fails (-EAGAIN), the
 * caller passes the last good heartbeat, which counts as no progress. */
#ifndef STALL_H
#define STALL_H

#include <stdint.h>

#define STALL_MS 100u

struct stall {
    uint64_t last_hb;
    uint64_t last_change_ns;
    int have_hb; /* a baseline heartbeat has been recorded */
    int armed;   /* a change has been seen */
};

void stall_init(struct stall *s);

/* Returns 1 if armed and hb has been unchanged for at least STALL_MS, else 0.
 * Any hb other than the last one is progress, so wraparound is fine.
 * *age_ns (may be NULL) gets the time since the last observed change, or 0
 * while unarmed. */
int stall_check(struct stall *s, uint64_t hb, uint64_t now_ns, uint64_t *age_ns);

#endif
