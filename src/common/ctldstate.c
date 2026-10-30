#include "ctldstate.h"

#include <errno.h>
#include <string.h>

#include "limiter.h"

void ctld_state_init(struct ctld_state *s) {
    memset(s, 0, sizeof(*s));
    strcpy(s->presets[0].name, "default");
    s->presets[0].bypass = 0;
    s->presets[0].ceiling_dbfs = LIMITER_DEFAULT_CEILING_DBFS;
    s->n_presets = 1;
    s->bypass = 0;
    s->ceiling_dbfs = LIMITER_DEFAULT_CEILING_DBFS;
    strcpy(s->active_preset, "default");
}

int ctld_state_set_bypass(struct ctld_state *s, uint32_t bypass) {
    if (!s || bypass > 1)
        return -EINVAL;
    s->bypass = bypass;
    return 0;
}

int ctld_state_set_ceiling(struct ctld_state *s, double dbfs) {
    if (!s || !limiter_ceiling_valid(dbfs))
        return -EINVAL;
    s->ceiling_dbfs = dbfs;
    return 0;
}

int ctld_state_set_preset(struct ctld_state *s, const char *name) {
    if (!s || !name)
        return -EINVAL;
    for (size_t i = 0; i < s->n_presets; i++) {
        if (strcmp(s->presets[i].name, name) == 0) {
            s->bypass = s->presets[i].bypass;
            s->ceiling_dbfs = s->presets[i].ceiling_dbfs;
            memcpy(s->active_preset, s->presets[i].name,
                   sizeof(s->active_preset));
            return 0;
        }
    }
    return -ENOENT;
}

void ctld_state_current(const struct ctld_state *s, struct param_block *out,
                        char *name_out) {
    if (out) {
        param_default(out);
        out->bypass = s->bypass;
        out->ceiling_dbfs = s->ceiling_dbfs;
    }
    if (name_out)
        memcpy(name_out, s->active_preset, sizeof(s->active_preset));
}

size_t ctld_state_list_presets(const struct ctld_state *s,
                               const char *names[], size_t max) {
    size_t n = s->n_presets < max ? s->n_presets : max;
    for (size_t i = 0; i < n; i++)
        names[i] = s->presets[i].name;
    return n;
}
