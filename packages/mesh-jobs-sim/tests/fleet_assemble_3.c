/* done_jobs_assemble_until_forgotten, seeds 9..11 (assemble.h). */
#include "assemble.h"

int main(void) {
    for (uint64_t seed = 9; seed < 12; seed++) done_jobs_assemble(seed);
    return mesh_check_done();
}
