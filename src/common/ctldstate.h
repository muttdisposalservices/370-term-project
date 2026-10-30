/* ctld's in-memory preset/param state (CtldOutline.md §4, "params.c"). Pure
 * logic, no sockets or shm, so it's unit-testable (CtldOutline.md §8).
 *
 * Presets are seeded once at startup and fixed for the life of the process:
 * the wire protocol (ctldproto.h) has no create/save-preset request, and
 * on-disk preset persistence is still an open item (CtldOutline.md §6), not
 * a committed decision. ctld_state_init seeds exactly one preset,
 * "default", matching param_default()'s values.
 *
 * The caller owns the struct; no allocation. */
#ifndef CTLDSTATE_H
#define CTLDSTATE_H

#include <stddef.h>
#include <stdint.h>

#include "ctldproto.h" /* CTLD_NAME_MAX */
#include "param.h"

#define CTLD_PRESET_MAX 16

struct ctld_preset {
    char name[CTLD_NAME_MAX + 1];
    uint32_t bypass;
    double ceiling_dbfs;
};

struct ctld_state {
    struct ctld_preset presets[CTLD_PRESET_MAX];
    size_t n_presets;
    uint32_t bypass;
    double ceiling_dbfs;
    char active_preset[CTLD_NAME_MAX + 1];
};

/* Seeds one preset ("default") and makes it current. */
void ctld_state_init(struct ctld_state *s);

/* Each returns 0, or -EINVAL (s NULL or the value out of range); the
 * current value is left untouched on failure. active_preset is not
 * cleared, so GET_PRESET still reports it even though the live values no
 * longer match it exactly -- same as any "modified since loaded" state. */
int ctld_state_set_bypass(struct ctld_state *s, uint32_t bypass);
int ctld_state_set_ceiling(struct ctld_state *s, double dbfs);

/* Activates the named preset: copies its values into current and sets
 * active_preset. Returns 0, -EINVAL (s or name NULL) or -ENOENT (no such
 * preset; current is untouched). */
int ctld_state_set_preset(struct ctld_state *s, const char *name);

/* Fills out with the current live values (magic/version set, ready to
 * format or publish) and copies the active preset's name into
 * name_out[CTLD_NAME_MAX + 1]. Either output pointer may be NULL. */
void ctld_state_current(const struct ctld_state *s, struct param_block *out,
                        char *name_out);

/* Fills names[] with pointers to this state's own preset-name strings
 * (valid as long as s is; the caller must not outlive s or mutate it while
 * holding them). Returns the count written, at most max. */
size_t ctld_state_list_presets(const struct ctld_state *s,
                               const char *names[], size_t max);

#endif
