/* The ctld -> audiod param block (ProposedStructure.md §3, issues.md C8/C11).
 * struct param_block is the slot layout of the ctld -> audiod triple buffer
 * (src/common/tbuf.h). ctld is the sole producer; audiod validates before
 * ever applying it (plan.md C11).
 *
 * tbuf_init zeroes every slot, and a zeroed param_block would otherwise pass
 * validation: ceiling_dbfs 0 is a valid (full-scale) ceiling, and bypass 0 is
 * a valid state, so neither field alone can tell "ctld published this" from
 * "never published" (issues.md C11, 2026-10-29). magic/version are what
 * makes that distinction: a never-published slot has magic 0, which can
 * never equal PARAM_MAGIC, and audiod checks them before anything else.
 *
 * Every field is fixed-width; _reserved pads ceiling_dbfs to its natural
 * 8-byte alignment explicitly, so the layout has no compiler-inserted
 * padding and is the same in every daemon. */
#ifndef PARAM_H
#define PARAM_H

#include <stdint.h>

#define PARAM_MAGIC   0x50524d31u /* "PRM1" */
#define PARAM_VERSION 1u

struct param_block {
    uint32_t magic;
    uint32_t version;
    uint32_t bypass;       /* 0 or 1: skip the EQ cascade */
    uint32_t _reserved;    /* must be 0; keeps ceiling_dbfs 8-byte aligned */
    double   ceiling_dbfs; /* limiter ceiling; see limiter_ceiling_valid() */
};

_Static_assert(sizeof(struct param_block) == 24,
               "param block must have no padding (24 B)");

/* audiod's compiled-in default (plan.md C11): bypass off, -6 dBFS ceiling. */
void param_default(struct param_block *p);

/* 1 if magic/version match, bypass is 0 or 1, and ceiling_dbfs passes
 * limiter_ceiling_valid(); else 0, including for p NULL. The magic check is
 * what rejects a zeroed (never-published) slot. */
int param_valid(const struct param_block *p);

#endif
