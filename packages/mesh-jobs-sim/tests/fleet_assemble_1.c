/* done_jobs_assemble_until_forgotten, seed 1 (assemble.h). */
#include "assemble.h"

int main(void) {
    done_jobs_assemble(1);
    scene_free();
    return mesh_check_done();
}
