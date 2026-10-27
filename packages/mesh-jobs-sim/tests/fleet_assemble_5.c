/* done_jobs_assemble_until_forgotten, seeds 15..17 (assemble.h). */
#include "assemble.h"

int main(void) {
    for (uint64_t seed = 15; seed < 18; seed++) done_jobs_assemble(seed);
    return mesh_check_done();
}
