/* ctld's in-memory preset/param state (src/ctl/ctldstate.c). */
#include "test.h"
#include "ctldstate.h"

#include <errno.h>
#include <math.h>
#include <string.h>

static void test_init_defaults(void) {
    struct ctld_state s;
    struct param_block p;
    char name[CTLD_NAME_MAX + 1];

    ctld_state_init(&s);
    ctld_state_current(&s, &p, name);
    TEST_CHECK(strcmp(name, "default") == 0);
    TEST_EQ(param_valid(&p), 1);
    TEST_EQ(p.bypass, 0u);
    TEST_EQ(p.ceiling_dbfs, -6.0);

    const char *names[CTLD_PRESET_MAX];
    TEST_EQ(ctld_state_list_presets(&s, names, CTLD_PRESET_MAX), (size_t)1);
    TEST_CHECK(strcmp(names[0], "default") == 0);
}

static void test_set_bypass_ceiling(void) {
    struct ctld_state s;
    struct param_block p;

    ctld_state_init(&s);
    TEST_EQ(ctld_state_set_bypass(&s, 1), 0);
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.bypass, 1u);

    TEST_EQ(ctld_state_set_bypass(&s, 2), -EINVAL);
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.bypass, 1u); /* unchanged on rejection */

    /* Switches back the other way too, not just 0 -> 1. */
    TEST_EQ(ctld_state_set_bypass(&s, 0), 0);
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.bypass, 0u);

    TEST_EQ(ctld_state_set_bypass(NULL, 0), -EINVAL);

    TEST_EQ(ctld_state_set_ceiling(&s, -12.0), 0);
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.ceiling_dbfs, -12.0);

    /* Ceiling boundary values: exactly 0 dBFS (full scale) and -0.0 are
     * valid (limiter_ceiling_valid: finite and <= 0); anything above 0,
     * even a hair, is rejected. */
    TEST_EQ(ctld_state_set_ceiling(&s, 0.0), 0);
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.ceiling_dbfs, 0.0);

    TEST_EQ(ctld_state_set_ceiling(&s, -0.0), 0);
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.ceiling_dbfs, -0.0);

    TEST_EQ(ctld_state_set_ceiling(&s, -12.0), 0); /* back to a known value */
    TEST_EQ(ctld_state_set_ceiling(&s, 1e-9), -EINVAL);
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.ceiling_dbfs, -12.0); /* unchanged */

    TEST_EQ(ctld_state_set_ceiling(&s, -200.0), 0); /* far below 0 is fine */
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.ceiling_dbfs, -200.0);

    TEST_EQ(ctld_state_set_ceiling(&s, 0.1), -EINVAL);
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.ceiling_dbfs, -200.0); /* unchanged */

    /* NAN is rejected (not finite) and, like every other rejection, leaves
     * the previous ceiling exactly as it was. */
    TEST_EQ(ctld_state_set_ceiling(&s, NAN), -EINVAL);
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.ceiling_dbfs, -200.0); /* still unchanged */

    TEST_EQ(ctld_state_set_ceiling(&s, INFINITY), -EINVAL);
    ctld_state_current(&s, &p, NULL);
    TEST_EQ(p.ceiling_dbfs, -200.0); /* still unchanged */

    TEST_EQ(ctld_state_set_ceiling(NULL, -6.0), -EINVAL);
}

/* Modifying the live values doesn't clear which preset was last activated
 * -- GET_PRESET still reports it, same as any "modified since loaded"
 * control surface. */
static void test_active_preset_name_sticky(void) {
    struct ctld_state s;
    char name[CTLD_NAME_MAX + 1];

    ctld_state_init(&s);
    ctld_state_set_bypass(&s, 1);
    ctld_state_set_ceiling(&s, -20.0);
    ctld_state_current(&s, NULL, name);
    TEST_CHECK(strcmp(name, "default") == 0);
}

static void test_set_preset(void) {
    struct ctld_state s;
    struct param_block p;
    char name[CTLD_NAME_MAX + 1];

    ctld_state_init(&s);
    ctld_state_set_bypass(&s, 1);
    ctld_state_set_ceiling(&s, -20.0);

    /* Re-activating "default" resets the live values. */
    TEST_EQ(ctld_state_set_preset(&s, "default"), 0);
    ctld_state_current(&s, &p, name);
    TEST_EQ(p.bypass, 0u);
    TEST_EQ(p.ceiling_dbfs, -6.0);
    TEST_CHECK(strcmp(name, "default") == 0);

    /* Unknown preset: -ENOENT, current left untouched. */
    ctld_state_set_bypass(&s, 1);
    TEST_EQ(ctld_state_set_preset(&s, "nope"), -ENOENT);
    ctld_state_current(&s, &p, name);
    TEST_EQ(p.bypass, 1u);
    TEST_CHECK(strcmp(name, "default") == 0);

    TEST_EQ(ctld_state_set_preset(&s, NULL), -EINVAL);
    TEST_EQ(ctld_state_set_preset(NULL, "default"), -EINVAL);
}

static void test_list_presets_bounded(void) {
    struct ctld_state s;
    const char *names[1];

    ctld_state_init(&s);
    TEST_EQ(ctld_state_list_presets(&s, names, 0), (size_t)0);
    TEST_EQ(ctld_state_list_presets(&s, names, 1), (size_t)1);
}

int main(void) {
    test_init_defaults();
    test_set_bypass_ceiling();
    test_active_preset_name_sticky();
    test_set_preset();
    test_list_presets_bounded();
    return test_failures();
}
