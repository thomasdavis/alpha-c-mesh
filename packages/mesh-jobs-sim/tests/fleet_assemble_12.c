/* done_jobs_assemble_until_forgotten, seed 12 (assemble.h). */
#include "assemble.h"

int main(void) {
    done_jobs_assemble(12);
    scene_free();
    return mesh_check_done();
}
