#include "param.h"
#include "limiter.h"

void param_default(struct param_block *p) {
    p->magic = PARAM_MAGIC;
    p->version = PARAM_VERSION;
    p->bypass = 0;
    p->_reserved = 0;
    p->ceiling_dbfs = LIMITER_DEFAULT_CEILING_DBFS;
}

int param_valid(const struct param_block *p) {
    return p && p->magic == PARAM_MAGIC && p->version == PARAM_VERSION &&
           (p->bypass == 0 || p->bypass == 1) &&
           limiter_ceiling_valid(p->ceiling_dbfs);
}
