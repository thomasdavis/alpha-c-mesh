/* done_jobs_assemble_until_forgotten, seed 2 (assemble.h). */
#include "assemble.h"

int main(void) {
    done_jobs_assemble(2);
    scene_free();
    return mesh_check_done();
}
