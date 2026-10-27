/* done_jobs_assemble_until_forgotten, seeds 21..23 (assemble.h). */
#include "assemble.h"

int main(void) {
    for (uint64_t seed = 21; seed < 24; seed++) done_jobs_assemble(seed);
    return mesh_check_done();
}
