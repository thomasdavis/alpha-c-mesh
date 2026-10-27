/* hello: the smallest mesh program, for lifecycle tests. op 0: arg * 3 + 7.
 * View: 0x0 magic "HELO" (set by init), 0x4 last arg, 0x8 last result.
 * Counters: 0 tasks, 1 inits.
 * Ported from alpha-rust-mesh programs/hello/src/lib.rs. */
#include "mesh/rt.h"

#define HELLO_MAGIC 0x4f4c4548u /* "HELO" */

static void hello_init(const mesh_ctx *ctx) {
    if (mesh_ctx_leader(ctx)) {
        mesh_ctx_set_u32(ctx, 0x0, HELLO_MAGIC);
        mesh_ctx_count(ctx, 1, 1);
    }
}

static uint32_t hello_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    if (op != MESH_OP_TASK) return MESH_R_BAD_OP;
    uint32_t r = arg * 3u + 7u;
    if (mesh_ctx_leader(ctx)) {
        mesh_ctx_set_u32(ctx, 0x4, arg);
        mesh_ctx_set_u32(ctx, 0x8, r);
        mesh_ctx_count(ctx, 0, 1);
    }
    return r;
}

MESH_PROGRAM(hello_init, hello_task);
