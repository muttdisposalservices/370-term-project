/* ctld's line-based socket protocol (CtldOutline.md §7). Pure parsing and
 * formatting only -- no socket I/O, so this is unit-testable without a
 * running daemon (CtldOutline.md §8's testability split: the socket/
 * event-loop glue stays in src/ctl/main.c and friends).
 *
 * Wire format: one ASCII line per request/response, verb first, arguments
 * space-separated, no JSON. The caller strips the trailing LF before
 * parsing and adds it back after formatting; these functions never see or
 * emit it. A preset name is 1-CTLD_NAME_MAX characters of [A-Za-z0-9_-],
 * which keeps it safe to use later as a filename (CtldOutline.md §6) and
 * unambiguous to re-parse out of a response line.
 *
 * Requests:
 *   GET_PRESET
 *   SET_PRESET <name>
 *   SET_BYPASS <0|1>
 *   SET_CEILING <dbfs>
 *   GET_METERS
 *   LIST_PRESETS
 *
 * Responses:
 *   OK
 *   ERR <code> <message>
 *   PRESET <name> BYPASS <0|1> CEILING <dbfs>
 *   METERS <peak_source> <peak_system> <peak_out> <gain_min> <xruns>
 *          <heartbeat> <mute_state> <mute_reason> <window>
 *   PRESETS <name1> <name2> ...
 *
 * Doubles are formatted with %.17g, which round-trips any double exactly. */
#ifndef CTLDPROTO_H
#define CTLDPROTO_H

#include <stddef.h>
#include <stdint.h>

#include "param.h"
#include "status.h"

#define CTLD_NAME_MAX 31 /* + NUL */

/* ERR response codes. */
#define CTLD_ERR_BAD_REQUEST    1 /* malformed or unknown request line */
#define CTLD_ERR_UNKNOWN_PRESET 2 /* SET_PRESET named a preset that doesn't exist */
#define CTLD_ERR_INVALID_VALUE  3 /* SET_BYPASS/SET_CEILING value rejected */
#define CTLD_ERR_NO_STATUS      4 /* audiod's status block isn't available yet */

enum ctld_req_type {
    CTLD_REQ_GET_PRESET,
    CTLD_REQ_SET_PRESET,
    CTLD_REQ_SET_BYPASS,
    CTLD_REQ_SET_CEILING,
    CTLD_REQ_GET_METERS,
    CTLD_REQ_LIST_PRESETS,
};

struct ctld_request {
    enum ctld_req_type type;
    char name[CTLD_NAME_MAX + 1]; /* SET_PRESET */
    uint32_t bypass;              /* SET_BYPASS: 0 or 1 */
    double ceiling_dbfs;          /* SET_CEILING */
};

/* Parses one line (NUL-terminated, no trailing \r or \n) into *req. Returns
 * 0, or -EINVAL for an unknown verb, wrong argument count, an out-of-range
 * value (bypass not 0/1, a non-finite/malformed/unparsed-tail ceiling), or a
 * name that's empty, over CTLD_NAME_MAX, or outside [A-Za-z0-9_-]. req is
 * untouched on failure; line or req NULL is -EINVAL. */
int ctld_proto_parse(const char *line, struct ctld_request *req);

/* Each formats one response line (no trailing \n) into buf (bufsz bytes).
 * Returns the number of bytes written (excluding the NUL), or -EINVAL if it
 * wouldn't fit, an argument is NULL, a name is outside [A-Za-z0-9_-], msg
 * contains \r or \n, or (fmt_preset) bypass is neither 0 nor 1. */
int ctld_proto_fmt_ok(char *buf, size_t bufsz);
int ctld_proto_fmt_err(char *buf, size_t bufsz, int code, const char *msg);
int ctld_proto_fmt_preset(char *buf, size_t bufsz, const char *name,
                          const struct param_block *p);
int ctld_proto_fmt_meters(char *buf, size_t bufsz, const struct status_block *st);
int ctld_proto_fmt_presets(char *buf, size_t bufsz, const char * const *names,
                           size_t count);

#endif
