/* done_jobs_assemble_until_forgotten, seeds 18..20 (assemble.h). */
#include "assemble.h"

int main(void) {
    for (uint64_t seed = 18; seed < 21; seed++) done_jobs_assemble(seed);
    return mesh_check_done();
}
