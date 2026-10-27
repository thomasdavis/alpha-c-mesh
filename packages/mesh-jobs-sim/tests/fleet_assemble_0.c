/* done_jobs_assemble_until_forgotten, seeds 0..2 (assemble.h). */
#include "assemble.h"

int main(void) {
    for (uint64_t seed = 0; seed < 3; seed++) done_jobs_assemble(seed);
    return mesh_check_done();
}
