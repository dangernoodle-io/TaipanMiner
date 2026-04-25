#include "asic_nonce_dedup.h"
#include <string.h>

void asic_nonce_dedup_reset(asic_nonce_dedup_t *d)
{
    memset(d, 0, sizeof(*d));
}

bool asic_nonce_dedup_check_and_insert(asic_nonce_dedup_t *d,
                                        uint8_t job_id,
                                        uint32_t nonce,
                                        uint32_t ver)
{
    for (int i = 0; i < ASIC_NONCE_DEDUP_SIZE; i++) {
        if (d->entries[i].job_id == job_id &&
            d->entries[i].nonce == nonce &&
            d->entries[i].ver == ver) {
            return true;  // duplicate
        }
    }
    d->entries[d->next_idx].job_id = job_id;
    d->entries[d->next_idx].nonce = nonce;
    d->entries[d->next_idx].ver = ver;
    d->next_idx = (uint8_t)((d->next_idx + 1) % ASIC_NONCE_DEDUP_SIZE);
    return false;
}
