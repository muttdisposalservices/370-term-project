/* ctld's socket protocol (src/common/ctldproto.c, CtldOutline.md §7). */
#include "test.h"
#include "ctldproto.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void test_parse_no_arg_verbs(void) {
    struct ctld_request r;

    TEST_EQ(ctld_proto_parse("GET_PRESET", &r), 0);
    TEST_EQ(r.type, CTLD_REQ_GET_PRESET);
    TEST_EQ(ctld_proto_parse("GET_METERS", &r), 0);
    TEST_EQ(r.type, CTLD_REQ_GET_METERS);
    TEST_EQ(ctld_proto_parse("LIST_PRESETS", &r), 0);
    TEST_EQ(r.type, CTLD_REQ_LIST_PRESETS);

    /* Leading/trailing/extra internal whitespace is tolerated. */
    TEST_EQ(ctld_proto_parse("  GET_PRESET  ", &r), 0);
    TEST_EQ(r.type, CTLD_REQ_GET_PRESET);

    /* A no-arg verb with an argument is rejected. */
    TEST_EQ(ctld_proto_parse("GET_PRESET x", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("GET_METERS x", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("LIST_PRESETS x", &r), -EINVAL);
}

static void test_parse_set_preset(void) {
    struct ctld_request r;

    TEST_EQ(ctld_proto_parse("SET_PRESET loud", &r), 0);
    TEST_EQ(r.type, CTLD_REQ_SET_PRESET);
    TEST_CHECK(strcmp(r.name, "loud") == 0);

    TEST_EQ(ctld_proto_parse("SET_PRESET Loud_Preset-2", &r), 0);
    TEST_CHECK(strcmp(r.name, "Loud_Preset-2") == 0);

    /* Missing argument, too many arguments, disallowed characters, and a
     * too-long name are all rejected. */
    TEST_EQ(ctld_proto_parse("SET_PRESET", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_PRESET a b", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_PRESET bad name", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_PRESET bad.name", &r), -EINVAL);
    char toolong[CTLD_NAME_MAX + 3];
    memset(toolong, 'a', sizeof(toolong) - 1);
    toolong[sizeof(toolong) - 1] = '\0';
    char line[sizeof(toolong) + 16];
    snprintf(line, sizeof(line), "SET_PRESET %s", toolong);
    TEST_EQ(ctld_proto_parse(line, &r), -EINVAL);

    /* Exactly CTLD_NAME_MAX is fine. */
    char exact[CTLD_NAME_MAX + 1];
    memset(exact, 'b', CTLD_NAME_MAX);
    exact[CTLD_NAME_MAX] = '\0';
    snprintf(line, sizeof(line), "SET_PRESET %s", exact);
    TEST_EQ(ctld_proto_parse(line, &r), 0);
    TEST_CHECK(strcmp(r.name, exact) == 0);
}

static void test_parse_set_bypass(void) {
    struct ctld_request r;

    TEST_EQ(ctld_proto_parse("SET_BYPASS 0", &r), 0);
    TEST_EQ(r.type, CTLD_REQ_SET_BYPASS);
    TEST_EQ(r.bypass, 0u);
    TEST_EQ(ctld_proto_parse("SET_BYPASS 1", &r), 0);
    TEST_EQ(r.bypass, 1u);

    TEST_EQ(ctld_proto_parse("SET_BYPASS", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_BYPASS 2", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_BYPASS -1", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_BYPASS 01", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_BYPASS true", &r), -EINVAL);
}

static void test_parse_set_ceiling(void) {
    struct ctld_request r;

    TEST_EQ(ctld_proto_parse("SET_CEILING -6", &r), 0);
    TEST_EQ(r.type, CTLD_REQ_SET_CEILING);
    TEST_EQ(r.ceiling_dbfs, -6.0);

    TEST_EQ(ctld_proto_parse("SET_CEILING -6.5", &r), 0);
    TEST_EQ(r.ceiling_dbfs, -6.5);

    TEST_EQ(ctld_proto_parse("SET_CEILING 0", &r), 0);
    TEST_EQ(r.ceiling_dbfs, 0.0);

    /* Malformed, non-finite, or trailing-junk values are all rejected (the
     * limiter-ceiling range itself is audiod's/param_valid's job, not the
     * protocol's -- this just needs a clean double). */
    TEST_EQ(ctld_proto_parse("SET_CEILING", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_CEILING nan", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_CEILING inf", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_CEILING -6x", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("SET_CEILING -6 -7", &r), -EINVAL);
}

static void test_parse_rejects(void) {
    struct ctld_request r;

    TEST_EQ(ctld_proto_parse(NULL, &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("GET_PRESET", NULL), -EINVAL);
    TEST_EQ(ctld_proto_parse("", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("   ", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("BOGUS_VERB", &r), -EINVAL);
    TEST_EQ(ctld_proto_parse("get_preset", &r), -EINVAL); /* case-sensitive */
}

static void test_fmt_ok_err(void) {
    char buf[64];

    TEST_EQ(ctld_proto_fmt_ok(buf, sizeof(buf)), 2);
    TEST_CHECK(strcmp(buf, "OK") == 0);
    TEST_EQ(ctld_proto_fmt_ok(NULL, sizeof(buf)), -EINVAL);

    int n = ctld_proto_fmt_err(buf, sizeof(buf), 3, "bad value");
    TEST_CHECK(n > 0);
    TEST_CHECK(strcmp(buf, "ERR 3 bad value") == 0);

    TEST_EQ(ctld_proto_fmt_err(buf, sizeof(buf), 1, NULL), -EINVAL);
    TEST_EQ(ctld_proto_fmt_err(buf, sizeof(buf), 1, "line1\nline2"), -EINVAL);

    /* Buffer too small. */
    char tiny[3];
    TEST_EQ(ctld_proto_fmt_err(tiny, sizeof(tiny), 1, "too long"), -EINVAL);
}

static void test_fmt_preset(void) {
    char buf[96];
    struct param_block p;
    param_default(&p);

    int n = ctld_proto_fmt_preset(buf, sizeof(buf), "flat", &p);
    TEST_CHECK(n > 0);
    TEST_CHECK(strcmp(buf, "PRESET flat BYPASS 0 CEILING -6") == 0);

    p.bypass = 1;
    p.ceiling_dbfs = 0.0;
    TEST_EQ(ctld_proto_fmt_preset(buf, sizeof(buf), "flat", &p), (int)strlen(buf));
    TEST_CHECK(strcmp(buf, "PRESET flat BYPASS 1 CEILING 0") == 0);

    TEST_EQ(ctld_proto_fmt_preset(NULL, sizeof(buf), "flat", &p), -EINVAL);
    TEST_EQ(ctld_proto_fmt_preset(buf, sizeof(buf), NULL, &p), -EINVAL);
    TEST_EQ(ctld_proto_fmt_preset(buf, sizeof(buf), "flat", NULL), -EINVAL);
    TEST_EQ(ctld_proto_fmt_preset(buf, sizeof(buf), "bad name", &p), -EINVAL);

    p.bypass = 2;
    TEST_EQ(ctld_proto_fmt_preset(buf, sizeof(buf), "flat", &p), -EINVAL);

    char tiny[4];
    param_default(&p);
    TEST_EQ(ctld_proto_fmt_preset(tiny, sizeof(tiny), "flat", &p), -EINVAL);
}

static void test_fmt_meters(void) {
    char buf[160];
    struct status_block st;
    memset(&st, 0, sizeof(st));
    st.heartbeat = 123;
    st.xruns = 4;
    st.meter_window = 7;
    st.peak_source = 0.5;
    st.peak_system = 0.25;
    st.peak_out = 0.125;
    st.gain_min = 1.0;
    st.mute_state = 2;
    st.mute_reason = 0;

    int n = ctld_proto_fmt_meters(buf, sizeof(buf), &st);
    TEST_CHECK(n > 0);
    TEST_CHECK(strcmp(buf, "METERS 0.5 0.25 0.125 1 4 123 2 0 7") == 0);

    TEST_EQ(ctld_proto_fmt_meters(NULL, sizeof(buf), &st), -EINVAL);
    TEST_EQ(ctld_proto_fmt_meters(buf, sizeof(buf), NULL), -EINVAL);

    char tiny[4];
    TEST_EQ(ctld_proto_fmt_meters(tiny, sizeof(tiny), &st), -EINVAL);
}

static void test_fmt_presets(void) {
    char buf[64];

    TEST_EQ(ctld_proto_fmt_presets(buf, sizeof(buf), NULL, 0), 7);
    TEST_CHECK(strcmp(buf, "PRESETS") == 0);

    const char *names[] = {"flat", "loud", "night"};
    int n = ctld_proto_fmt_presets(buf, sizeof(buf), names, 3);
    TEST_CHECK(n > 0);
    TEST_CHECK(strcmp(buf, "PRESETS flat loud night") == 0);

    TEST_EQ(ctld_proto_fmt_presets(NULL, sizeof(buf), names, 3), -EINVAL);
    TEST_EQ(ctld_proto_fmt_presets(buf, sizeof(buf), NULL, 1), -EINVAL);

    const char *bad_names[] = {"flat", "bad name"};
    TEST_EQ(ctld_proto_fmt_presets(buf, sizeof(buf), bad_names, 2), -EINVAL);

    const char *null_name[] = {NULL};
    TEST_EQ(ctld_proto_fmt_presets(buf, sizeof(buf), null_name, 1), -EINVAL);

    char tiny[9]; /* fits "PRESETS" (7) + NUL but not a following name */
    TEST_EQ(ctld_proto_fmt_presets(tiny, sizeof(tiny), names, 3), -EINVAL);
}

int main(void) {
    test_parse_no_arg_verbs();
    test_parse_set_preset();
    test_parse_set_bypass();
    test_parse_set_ceiling();
    test_parse_rejects();
    test_fmt_ok_err();
    test_fmt_preset();
    test_fmt_meters();
    test_fmt_presets();
    return test_failures();
}
