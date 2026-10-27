/* done_jobs_assemble_until_forgotten, seeds 12..14 (assemble.h). */
#include "assemble.h"

int main(void) {
    for (uint64_t seed = 12; seed < 15; seed++) done_jobs_assemble(seed);
    return mesh_check_done();
}
