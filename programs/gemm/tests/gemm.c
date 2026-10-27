/* gemm on exec's node loop: OP_CHECK's C against mesh-ref's f64 reference
 * (fused multiply-adds round differently, so within a relative error), the
 * blocked kernel against the plain loop on sizes off the tile grid, timed
 * runs, the job record, sizes and threads. */
#include "harness.h"
#include "../src/gemm.h"
#include "mesh/ref/gemm.h"


int main(void) {
    /* The kernel against the plain loop (tiles, edges, several threads). */
    const size_t ns[] = {1, 5, 17, 64, 100, 257};
    for (size_t i = 0; i < sizeof ns / sizeof *ns; i++) {
        size_t n = ns[i];
        float *a = malloc(sizeof(float) * n * n), *b = malloc(sizeof(float) * n * n), *c = malloc(sizeof(float) * n * n), *p = malloc(sizeof(float) * n * n);
        mesh_gemm_inputs(n, a, b);
        gemm_fast(n, a, b, c, false);
        gemm_plain(n, a, b, p);
        /* Both against the f64 reference (mesh-ref's metric: relative to the sum of |terms|). */
        CHECK(mesh_gemm_max_rel_err(n, c) < 1e-5);
        CHECK(mesh_gemm_max_rel_err(n, p) < 1e-5);
        free(a), free(b), free(c), free(p);
    }
    mesh_mailbox mb = mesh_mailbox_test();
    h_install(mb, 0, "gemm", 1, 0x100000, 0x8000);
    h_slot h = {.mb = mb, .slot = 0, .prog = {gemm_init, gemm_task, NULL}};
    h_start(&h);
    CHECK_U(h_call(&h, MESH_OP_INIT, 0), MESH_R_NONE);
    size_t v = 0x100000;
    CHECK_U(mesh_mailbox_get(mb, v), GEMM_MAGIC);
    CHECK_U(mesh_mailbox_get(mb, v + GEMM_V_SIZE), 1024);
    CHECK_U(mesh_mailbox_get(mb, v + GEMM_V_IMPL), 2);
    /* OP_CHECK 48: C in the view, against the reference. */
    CHECK_U(h_call(&h, GEMM_OP_CHECK, 48), 48);
    float c[48 * 48];
    mesh_mailbox_read(mb, v + GEMM_V_C, (uint8_t *)c, sizeof c);
    CHECK(mesh_gemm_max_rel_err(48, c) < 1e-4);
    CHECK_U(h_call(&h, GEMM_OP_CHECK, 65), MESH_R_BAD_OP);
    /* Sizes, a timed run, a job item and its record. */
    CHECK_U(h_call(&h, GEMM_OP_SIZE, 96), 96);
    CHECK_U(h_call(&h, GEMM_OP_SIZE, 9000), MESH_R_BAD_OP);
    (void)h_call(&h, MESH_OP_TASK, 0);
    CHECK_U(mesh_mailbox_get(mb, v + GEMM_V_N), 96);
    CHECK_U(mesh_mailbox_get(mb, v + GEMM_V_RUNS), 1);
    uint32_t t = h.next;
    uint32_t us = h_call(&h, MESH_OP_JOB, 0);
    CHECK(us < MESH_R_JOB_FAIL);
    CHECK_U(mesh_mailbox_get(mb, v + GEMM_V_JOB_OUT + 8 * (t % MESH_PROG_RESULTS)), 96);
    CHECK(mesh_mailbox_get(mb, v + GEMM_V_BEST) != 0);
    (void)h_call(&h, GEMM_OP_PLAIN, 32);
    (void)h_call(&h, GEMM_OP_SINGLE, 32);
    CHECK(mesh_mailbox_get(mb, v + GEMM_V_PLAIN) != 0 && mesh_mailbox_get(mb, v + GEMM_V_SINGLE) != 0);
    CHECK_U(h_call(&h, GEMM_OP_THREADS, 3), 3);
    CHECK_U(mesh_mailbox_get(mb, v + GEMM_V_THREADS), 3);
    CHECK_U(h_call(&h, 99, 0), MESH_R_BAD_OP);
    CHECK_U(mesh_mailbox_get(mb, mesh_prog_ctl(0) + MESH_P_FAULT), 0);
    h_stop(&h);
    free(mb.base);
    return mesh_check_done();
}
