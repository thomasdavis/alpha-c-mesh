/* pipesink as a native worker process under meshd. */
#include "mesh/worker.h"
#include "pipesink.h"

int main(void) {
    static const mesh_worker_program p = {pipesink_init, pipesink_task, NULL};
    mesh_worker_run(&p);
}
