#include "stratum_reqid.h"
#include <string.h>

void stratum_reqid_reset(stratum_reqid_table_t *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

void stratum_reqid_register(stratum_reqid_table_t *t, int id, stratum_reqid_kind_t kind)
{
    if (!t) return;

    // Prefer an empty slot.
    for (int i = 0; i < STRATUM_REQID_MAX_INFLIGHT; i++) {
        if (!t->slots[i].used) {
            t->slots[i].id = id;
            t->slots[i].kind = kind;
            t->slots[i].used = true;
            return;
        }
    }

    // Table full: evict slot 0 (oldest by construction -- slots fill in
    // order and are only vacated by stratum_reqid_take(), so slot 0 being
    // reached here means every slot is occupied; shift left and append).
    memmove(&t->slots[0], &t->slots[1], sizeof(t->slots[0]) * (STRATUM_REQID_MAX_INFLIGHT - 1));
    t->slots[STRATUM_REQID_MAX_INFLIGHT - 1].id = id;
    t->slots[STRATUM_REQID_MAX_INFLIGHT - 1].kind = kind;
    t->slots[STRATUM_REQID_MAX_INFLIGHT - 1].used = true;
}

stratum_reqid_kind_t stratum_reqid_take(stratum_reqid_table_t *t, int id)
{
    if (!t) return STRATUM_REQID_NONE;

    for (int i = 0; i < STRATUM_REQID_MAX_INFLIGHT; i++) {
        if (t->slots[i].used && t->slots[i].id == id) {
            stratum_reqid_kind_t kind = t->slots[i].kind;
            t->slots[i].used = false;
            return kind;
        }
    }
    return STRATUM_REQID_NONE;
}
