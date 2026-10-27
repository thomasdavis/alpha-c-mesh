/* a_full_store_refuses_and_keeps_listed_jobs, seeds 0..1 (payload.h). */
#include "payload.h"

int main(void) {
    for (uint64_t seed = 0; seed < 2; seed++) a_full_store_refuses(seed);
    return mesh_check_done();
}
