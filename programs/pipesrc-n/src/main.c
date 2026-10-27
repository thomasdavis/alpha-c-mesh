/* pipesrc-n as a native worker process under meshd. */
#include "mesh/worker.h"
#include "pipesrc.h"

int main(void) {
    static const mesh_worker_program p = {pipesrc_init, pipesrc_task, NULL};
    mesh_worker_run(&p);
}
