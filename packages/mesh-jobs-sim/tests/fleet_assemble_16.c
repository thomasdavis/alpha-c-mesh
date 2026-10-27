/* done_jobs_assemble_until_forgotten, seed 16 (assemble.h). */
#include "assemble.h"

int main(void) {
    done_jobs_assemble(16);
    scene_free();
    return mesh_check_done();
}
