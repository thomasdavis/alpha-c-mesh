/* done_jobs_assemble_until_forgotten, seed 3 (assemble.h). */
#include "assemble.h"

int main(void) {
    done_jobs_assemble(3);
    scene_free();
    return mesh_check_done();
}
