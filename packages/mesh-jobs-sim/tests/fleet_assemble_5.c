/* done_jobs_assemble_until_forgotten, seed 5 (assemble.h). */
#include "assemble.h"

int main(void) {
    done_jobs_assemble(5);
    scene_free();
    return mesh_check_done();
}
