/* done_jobs_assemble_until_forgotten, seeds 6..8 (assemble.h). */
#include "assemble.h"

int main(void) {
    for (uint64_t seed = 6; seed < 9; seed++) done_jobs_assemble(seed);
    return mesh_check_done();
}
