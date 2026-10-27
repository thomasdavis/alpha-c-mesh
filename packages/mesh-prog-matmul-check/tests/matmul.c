/* matmul on the simulator against @ajax/mesh-ref-matmul, bitwise: matrices
 * generated on the device, every C tile (dimensions that are not multiples of
 * the tile), y = A x, both copied out through the view's window; bad
 * arguments refused. */
#include "mesh/check.h"
#include "mesh/ref/matmul.h"
#include "mesh/rt/sim_mailbox.h"

#include "../../../programs/matmul/src/matmul.c"

#include <stdlib.h>

static uint8_t *mb, *view;

static void set(uint32_t field, uint32_t v) { CHECK_U(mesh_sim_call(mb, 0, MATMUL_OP_SET, field << 24 | v), MESH_R_NONE); }

static void run(uint32_t m, uint32_t k, uint32_t n, uint32_t sa, uint32_t sb) {
    set(0, m), set(1, k), set(2, n), set(3, sa), set(4, sb);
    uint32_t lens[3] = {m * k, k * n, k};
    for (uint32_t w = 0; w < 3; w++)
        for (uint32_t c = 0; c * MATMUL_CHUNK < lens[w]; c++) CHECK_U(mesh_sim_call(mb, 0, MATMUL_OP_GEN, w << 24 | c), MESH_R_NONE);
    uint32_t tiles = ((m + 63) / 64) * ((n + 63) / 64);
    uint32_t ts[64];
    for (uint32_t t0 = 0; t0 < tiles; t0 += 32) {
        uint32_t b = tiles - t0 < 32 ? tiles - t0 : 32;
        for (uint32_t i = 0; i < b; i++) ts[i] = mesh_sim_publish(mb, 0, MATMUL_OP_MM, t0 + i);
        for (uint32_t i = 0; i < b; i++) {
            uint32_t r = 0;
            CHECK(mesh_sim_wait(mb, 0, ts[i], &r, 120000));
            CHECK_U(r, MESH_R_NONE);
        }
    }
    CHECK_U(mesh_sim_call(mb, 0, MATMUL_OP_MM, tiles), MESH_R_BAD_OP);
    for (uint32_t b = 0; b * 64 < m; b++) CHECK_U(mesh_sim_call(mb, 0, MATMUL_OP_MV, b), MESH_R_NONE);
    float *a = malloc(sizeof(float) * m * k), *bm = malloc(sizeof(float) * k * n), *x = malloc(sizeof(float) * k);
    float *c = malloc(sizeof(float) * m * n), *y = malloc(sizeof(float) * m);
    mesh_matmul_matrix(sa, (size_t)m * k, a);
    mesh_matmul_matrix(sb, (size_t)k * n, bm);
    mesh_matmul_matrix(sb ^ 0x55555555u, k, x);
    mesh_matmul(a, bm, m, k, n, 1, c);
    mesh_matvec(a, x, m, k, y);
    uint32_t bad = 0;
    for (uint32_t ch = 0; ch * MATMUL_CHUNK < m * n; ch++) {
        CHECK_U(mesh_sim_call(mb, 0, MATMUL_OP_COPY, ch), MESH_R_NONE);
        for (uint32_t i = ch * MATMUL_CHUNK; i < m * n && i < (ch + 1) * MATMUL_CHUNK; i++)
            bad += mesh_sim_u32(view + MATMUL_V_WINDOW + 4 * (i - ch * MATMUL_CHUNK)) != mesh_f32_bits(c[i]);
    }
    CHECK_U(bad, 0);
    CHECK_U(mesh_sim_call(mb, 0, MATMUL_OP_COPY, 1u << 24), MESH_R_NONE);
    for (uint32_t i = 0; i < m; i++) bad += mesh_sim_u32(view + MATMUL_V_WINDOW + 4 * i) != mesh_f32_bits(y[i]);
    CHECK_U(bad, 0);
    free(a), free(bm), free(x), free(c), free(y);
}

int main(void) {
    mb = mesh_sim_mailbox();
    uint8_t *scratch = calloc(1, MATMUL_SCRATCH_MIN);
    CHECK(scratch != NULL);
    mesh_sim_prog p = {.name = "matmul", .view_bytes = 0x41000, .nodes = 2, .warps = 8, .shared_bytes = 0x2000,
                       .scratch = scratch, .scratch_bytes = MATMUL_SCRATCH_MIN};
    view = mesh_sim_install(mb, 0, &p);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, 0, &p, mesh_main);
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_INIT, 0), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view), MATMUL_MAGIC);
    CHECK_U(mesh_sim_u32(view + MATMUL_V_DIMS), 768);
    run(100, 70, 130, 7, 9);
    run(64, 64, 64, 1, 2);
    run(1, 33, 5, 3, 4);
    CHECK_U(mesh_sim_call(mb, 0, MATMUL_OP_SET, 5u << 24), MESH_R_BAD_OP);
    CHECK_U(mesh_sim_call(mb, 0, MATMUL_OP_GEN, 3u << 24), MESH_R_BAD_OP);
    set(0, 0); /* invalid M: everything but SET refused */
    CHECK_U(mesh_sim_call(mb, 0, MATMUL_OP_MM, 0), MESH_R_BAD_OP);
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, 0) + MESH_P_FAULT), 0);
    mesh_sim_halt(mb, 0, l);
    /* A node of the wrong shape refuses. */
    mesh_sim_prog q = p;
    q.warps = 4;
    mesh_sim_install(mb, 0, &q);
    l = mesh_sim_launch_prog(mb, 0, &q, mesh_main);
    set(0, 64);
    CHECK_U(mesh_sim_call(mb, 0, MATMUL_OP_MM, 0), MESH_R_BAD_OP);
    mesh_sim_halt(mb, 0, l);
    free(scratch);
    mesh_sim_mailbox_free(mb);
    return mesh_check_done();
}
