/* a_full_store_refuses_and_keeps_listed_jobs, seeds 2..3 (payload.h). */
#include "payload.h"

int main(void) {
    for (uint64_t seed = 2; seed < 4; seed++) a_full_store_refuses(seed);
    return mesh_check_done();
}
