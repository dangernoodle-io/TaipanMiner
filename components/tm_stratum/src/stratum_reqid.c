#include "stratum_reqid.h"
#include <string.h>

void stratum_reqid_reset(stratum_reqid_table_t *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

// Shared register implementation: `rec` is NULL for every non-SUBMIT
// registration (and the caller-facing stratum_reqid_register() itself never
// carries one, even for a bare SUBMIT with no payload); stratum_reqid_
// register_submit() is the only caller that passes a non-NULL rec.
static void register_slot(stratum_reqid_table_t *t, int id, stratum_reqid_kind_t kind,
                          const stratum_accepted_share_t *rec)
{
    stratum_reqid_slot_t new_slot;
    new_slot.id = id;
    new_slot.kind = kind;
    new_slot.used = true;
    if (rec) {
        new_slot.share = *rec;
    } else {
        memset(&new_slot.share, 0, sizeof(new_slot.share));
    }

    // Prefer an empty slot.
    for (int i = 0; i < STRATUM_REQID_MAX_INFLIGHT; i++) {
        if (!t->slots[i].used) {
            t->slots[i] = new_slot;
            return;
        }
    }

    // Table full: evict slot 0 (oldest by construction -- slots fill in
    // order and are only vacated by stratum_reqid_take(), so slot 0 being
    // reached here means every slot is occupied; shift left and append).
    // The id/kind AND share payload move together -- there is only ever one
    // eviction operation per slot (TA-571 finding #2).
    memmove(&t->slots[0], &t->slots[1], sizeof(t->slots[0]) * (STRATUM_REQID_MAX_INFLIGHT - 1));
    t->slots[STRATUM_REQID_MAX_INFLIGHT - 1] = new_slot;
}

void stratum_reqid_register(stratum_reqid_table_t *t, int id, stratum_reqid_kind_t kind)
{
    if (!t) return;
    register_slot(t, id, kind, NULL);
}

void stratum_reqid_register_submit(stratum_reqid_table_t *t, int id, const stratum_accepted_share_t *rec)
{
    if (!t || !rec) return;
    register_slot(t, id, STRATUM_REQID_SUBMIT, rec);
}

stratum_reqid_kind_t stratum_reqid_take(stratum_reqid_table_t *t, int id, stratum_accepted_share_t *share_out)
{
    if (!t) return STRATUM_REQID_NONE;

    for (int i = 0; i < STRATUM_REQID_MAX_INFLIGHT; i++) {
        if (t->slots[i].used && t->slots[i].id == id) {
            stratum_reqid_kind_t kind = t->slots[i].kind;
            if (kind == STRATUM_REQID_SUBMIT && share_out) {
                *share_out = t->slots[i].share;
            }
            t->slots[i].used = false;
            return kind;
        }
    }
    return STRATUM_REQID_NONE;
}
