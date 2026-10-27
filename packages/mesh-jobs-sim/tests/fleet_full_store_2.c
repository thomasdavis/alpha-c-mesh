/* a_full_store_refuses_and_keeps_listed_jobs, seeds 4..5 (payload.h). */
#include "payload.h"

int main(void) {
    for (uint64_t seed = 4; seed < 6; seed++) a_full_store_refuses(seed);
    return mesh_check_done();
}
