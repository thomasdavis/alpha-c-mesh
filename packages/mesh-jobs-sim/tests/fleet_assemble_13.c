/* done_jobs_assemble_until_forgotten, seed 13 (assemble.h). */
#include "assemble.h"

int main(void) {
    done_jobs_assemble(13);
    scene_free();
    return mesh_check_done();
}
