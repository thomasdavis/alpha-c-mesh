/* gemm as a native worker process under meshd. */
#include "gemm.h"
#include "mesh/ref/gemm.h"
#include "mesh/worker.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    /* Warm the kernel and the pages once before meshd hears `ready`. */
    float *a = malloc(sizeof(float) * 64 * 64), *b = malloc(sizeof(float) * 64 * 64), *c = malloc(sizeof(float) * 64 * 64);
    if (a && b && c) {
        mesh_gemm_inputs(64, a, b);
        gemm_fast(64, a, b, c, false);
    }
    free(a), free(b), free(c);
    fprintf(stderr, "gemm: blocked C (4x16 tiles), %zu threads\n", gemm_threads());
    static const mesh_worker_program p = {gemm_init, gemm_task, NULL};
    mesh_worker_run(&p);
}
