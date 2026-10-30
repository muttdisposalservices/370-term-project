#include "ctldproto.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct token {
    const char *p;
    size_t len;
};

static const char *skip_ws(const char *s) {
    while (*s == ' ')
        s++;
    return s;
}

static size_t token_len(const char *s) {
    size_t n = 0;
    while (s[n] != '\0' && s[n] != ' ')
        n++;
    return n;
}

/* Splits line into up to max tokens separated by one or more ' '. Leading/
 * trailing whitespace is ignored. Returns the token count, or -1 if there
 * are more than max. */
static int tokenize(const char *line, struct token *toks, int max) {
    int n = 0;
    const char *s = skip_ws(line);
    while (*s != '\0') {
        if (n >= max)
            return -1;
        size_t len = token_len(s);
        toks[n].p = s;
        toks[n].len = len;
        n++;
        s = skip_ws(s + len);
    }
    return n;
}

static int tok_eq(struct token t, const char *lit) {
    size_t lit_len = strlen(lit);
    return t.len == lit_len && memcmp(t.p, lit, lit_len) == 0;
}

static int is_name_char(unsigned char c) {
    return isalnum(c) || c == '_' || c == '-';
}

static int tok_to_name(struct token t, char *out) {
    if (t.len == 0 || t.len > CTLD_NAME_MAX)
        return -1;
    for (size_t i = 0; i < t.len; i++) {
        if (!is_name_char((unsigned char)t.p[i]))
            return -1;
    }
    memcpy(out, t.p, t.len);
    out[t.len] = '\0';
    return 0;
}

static int tok_to_double(struct token t, double *out) {
    char buf[64];
    if (t.len == 0 || t.len >= sizeof(buf))
        return -1;
    memcpy(buf, t.p, t.len);
    buf[t.len] = '\0';
    char *end = NULL;
    errno = 0;
    double v = strtod(buf, &end);
    if (errno == ERANGE || end != buf + t.len || !isfinite(v))
        return -1;
    *out = v;
    return 0;
}

static int tok_to_bit(struct token t, uint32_t *out) {
    if (t.len != 1 || (t.p[0] != '0' && t.p[0] != '1'))
        return -1;
    *out = (uint32_t)(t.p[0] - '0');
    return 0;
}

int ctld_proto_parse(const char *line, struct ctld_request *req) {
    if (!line || !req)
        return -EINVAL;

    struct token toks[2];
    int n = tokenize(line, toks, 2);
    if (n < 1)
        return -EINVAL;

    if (tok_eq(toks[0], "GET_PRESET")) {
        if (n != 1)
            return -EINVAL;
        req->type = CTLD_REQ_GET_PRESET;
        return 0;
    }
    if (tok_eq(toks[0], "SET_PRESET")) {
        if (n != 2 || tok_to_name(toks[1], req->name) != 0)
            return -EINVAL;
        req->type = CTLD_REQ_SET_PRESET;
        return 0;
    }
    if (tok_eq(toks[0], "SET_BYPASS")) {
        if (n != 2 || tok_to_bit(toks[1], &req->bypass) != 0)
            return -EINVAL;
        req->type = CTLD_REQ_SET_BYPASS;
        return 0;
    }
    if (tok_eq(toks[0], "SET_CEILING")) {
        if (n != 2 || tok_to_double(toks[1], &req->ceiling_dbfs) != 0)
            return -EINVAL;
        req->type = CTLD_REQ_SET_CEILING;
        return 0;
    }
    if (tok_eq(toks[0], "GET_METERS")) {
        if (n != 1)
            return -EINVAL;
        req->type = CTLD_REQ_GET_METERS;
        return 0;
    }
    if (tok_eq(toks[0], "LIST_PRESETS")) {
        if (n != 1)
            return -EINVAL;
        req->type = CTLD_REQ_LIST_PRESETS;
        return 0;
    }
    return -EINVAL;
}

int ctld_proto_fmt_ok(char *buf, size_t bufsz) {
    if (!buf)
        return -EINVAL;
    int n = snprintf(buf, bufsz, "OK");
    if (n < 0 || (size_t)n >= bufsz)
        return -EINVAL;
    return n;
}

int ctld_proto_fmt_err(char *buf, size_t bufsz, int code, const char *msg) {
    if (!buf || !msg || strchr(msg, '\n') || strchr(msg, '\r'))
        return -EINVAL;
    int n = snprintf(buf, bufsz, "ERR %d %s", code, msg);
    if (n < 0 || (size_t)n >= bufsz)
        return -EINVAL;
    return n;
}

int ctld_proto_fmt_preset(char *buf, size_t bufsz, const char *name,
                          const struct param_block *p) {
    if (!buf || !name || !p || p->bypass > 1)
        return -EINVAL;
    for (const char *c = name; *c != '\0'; c++) {
        if (!is_name_char((unsigned char)*c))
            return -EINVAL;
    }
    int n = snprintf(buf, bufsz, "PRESET %s BYPASS %" PRIu32 " CEILING %.17g",
                     name, p->bypass, p->ceiling_dbfs);
    if (n < 0 || (size_t)n >= bufsz)
        return -EINVAL;
    return n;
}

int ctld_proto_fmt_meters(char *buf, size_t bufsz, const struct status_block *st) {
    if (!buf || !st)
        return -EINVAL;
    int n = snprintf(buf, bufsz,
                     "METERS %.17g %.17g %.17g %.17g %" PRIu64 " %" PRIu64
                     " %" PRIu32 " %" PRIu32 " %" PRIu64,
                     st->peak_source, st->peak_system, st->peak_out,
                     st->gain_min, st->xruns, st->heartbeat, st->mute_state,
                     st->mute_reason, st->meter_window);
    if (n < 0 || (size_t)n >= bufsz)
        return -EINVAL;
    return n;
}

int ctld_proto_fmt_presets(char *buf, size_t bufsz, const char * const *names,
                           size_t count) {
    if (!buf || (!names && count != 0))
        return -EINVAL;

    int n = snprintf(buf, bufsz, "PRESETS");
    if (n < 0 || (size_t)n >= bufsz)
        return -EINVAL;
    size_t pos = (size_t)n;

    for (size_t i = 0; i < count; i++) {
        if (!names[i])
            return -EINVAL;
        for (const char *c = names[i]; *c != '\0'; c++) {
            if (!is_name_char((unsigned char)*c))
                return -EINVAL;
        }
        int m = snprintf(buf + pos, bufsz - pos, " %s", names[i]);
        if (m < 0 || (size_t)m >= bufsz - pos)
            return -EINVAL;
        pos += (size_t)m;
    }
    return (int)pos;
}
