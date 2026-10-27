/* done_jobs_assemble_until_forgotten, seeds 3..5 (assemble.h). */
#include "assemble.h"

int main(void) {
    for (uint64_t seed = 3; seed < 6; seed++) done_jobs_assemble(seed);
    return mesh_check_done();
}
