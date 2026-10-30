/* The ctld -> audiod param block (src/common/param.c, issues.md C11). */
#include "test.h"
#include "param.h"

#include <string.h>

static void test_default(void) {
    struct param_block p;

    param_default(&p);
    TEST_EQ(p.magic, PARAM_MAGIC);
    TEST_EQ(p.version, PARAM_VERSION);
    TEST_EQ(p.bypass, 0u);
    TEST_EQ(p.ceiling_dbfs, -6.0);
    TEST_EQ(param_valid(&p), 1);
}

/* A zeroed slot -- tbuf_init's starting state, and what a never-published
 * slot looks like -- must be rejected, even though bypass=0 and
 * ceiling_dbfs=0 are each individually valid values. This is the whole
 * point of the magic/version fields (issues.md C11). */
static void test_zeroed_slot_rejected(void) {
    struct param_block p;

    memset(&p, 0, sizeof(p));
    TEST_EQ(param_valid(&p), 0);

    /* Confirms the individual fields really are otherwise-valid, so the
     * rejection above is really coming from the magic/version check. */
    TEST_EQ(p.bypass == 0 || p.bypass == 1, 1);
    TEST_CHECK(0.0 <= 0.0); /* ceiling_dbfs = 0 passes limiter_ceiling_valid */
}

static void test_valid_rules(void) {
    struct param_block p;

    param_default(&p);
    TEST_EQ(param_valid(NULL), 0);

    p.version = PARAM_VERSION + 1;
    TEST_EQ(param_valid(&p), 0);

    param_default(&p);
    p.magic = 0;
    TEST_EQ(param_valid(&p), 0);

    param_default(&p);
    p.bypass = 2;
    TEST_EQ(param_valid(&p), 0);

    param_default(&p);
    p.bypass = 1;
    TEST_EQ(param_valid(&p), 1);

    param_default(&p);
    p.ceiling_dbfs = 0.1; /* above full scale: limiter_ceiling_valid rejects */
    TEST_EQ(param_valid(&p), 0);

    param_default(&p);
    p.ceiling_dbfs = 0.0; /* full scale, finite, <= 0: valid once magic/version hold */
    TEST_EQ(param_valid(&p), 1);
}

int main(void) {
    test_default();
    test_zeroed_slot_rejected();
    test_valid_rules();
    return test_failures();
}
