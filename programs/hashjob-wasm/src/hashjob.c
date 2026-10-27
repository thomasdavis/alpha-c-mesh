/* hashjob: a CPU job program (job arrays, docs/plan-fleet-scheduling.md F5) for the wasm executor
 * (alpha-rust-mesh programs/hashjob/wasm), so a job array runs on the CPU of every member. OP_JOB
 * item i (i = the u64 meshd wrote at V_JOB_BASE + arg): the XOR of the first LE u32 of
 * sha256d(LE8(i * K + j)) for j in 0..K (K = the manifest's param), and the item's job_out record:
 * the first 16 bytes of sha256d(LE8(i)) at V_OUT + 16 (ticket % 64). Setup ops make test items:
 * 3 item `arg` fails (R_JOB_FAIL), 4 item `arg` traps, 5 clears both. */
#include "mesh/abi/ring.h"
#include "mesh/ref/sha256d.h"
#include "mesh/rt_wasm.h"

#define V_MAGIC 0x00u
#define V_JOB_BASE 0x08u
#define V_FAIL_ITEM 0x10u /* item + 1 (low 32 bits), 0: none */
#define V_TRAP_ITEM 0x18u
#define V_ITEMS 0x20u
#define V_HASHES 0x28u
#define V_LAST 0x30u
#define V_OUT 0x100u
#define OUT_REC 16u
#define MAGIC 0x4a485348u /* "HSHJ" */

#define OP_FAIL_ITEM 3u
#define OP_TRAP_ITEM 4u
#define OP_CLEAR 5u

static void init(const mesh_ctx *ctx) {
    mesh_view v = mesh_ctx_view(ctx);
    mesh_view_set_u32(v, V_MAGIC, MAGIC);
    mesh_view_set_u32(v, V_FAIL_ITEM, 0);
    mesh_view_set_u32(v, V_TRAP_ITEM, 0);
}

static void le8(uint64_t x, uint8_t out[8]) {
    for (int k = 0; k < 8; k++) out[k] = (uint8_t)(x >> (8 * k));
}

static uint32_t item(const mesh_ctx *ctx, uint32_t arg) {
    mesh_view v = mesh_ctx_view(ctx);
    uint64_t i = mesh_view_u64(v, V_JOB_BASE) + arg;
    if (mesh_view_u32(v, V_FAIL_ITEM) == (uint32_t)i + 1u) {
        mesh_ctx_count(ctx, 2, 1);
        return MESH_R_JOB_FAIL;
    }
    if (mesh_view_u32(v, V_TRAP_ITEM) == (uint32_t)i + 1u) MESH_TRAP();
    uint32_t p = mesh_ctx_param(ctx);
    uint64_t k = p > 1 ? p : 1;
    uint32_t fold = 0;
    uint8_t b[8], h[32];
    for (uint64_t j = 0; j < k; j++) {
        le8(i * k + j, b);
        mesh_sha256d(b, 8, h);
        fold ^= (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
    }
    le8(i, b);
    mesh_sha256d(b, 8, h);
    mesh_view_write(v, V_OUT + OUT_REC * (mesh_ctx_ticket(ctx) % MESH_PROG_RESULTS), h, OUT_REC);
    mesh_view_add64(v, V_ITEMS, 1);
    mesh_view_add64(v, V_HASHES, k);
    mesh_view_set_u64(v, V_LAST, i);
    mesh_ctx_count(ctx, 0, 1);
    uint64_t per = k / 1024;
    mesh_ctx_count(ctx, 1, (uint32_t)(per > 1 ? per : 1));
    /* R_JOB_FAIL is reserved: a fold that equals it is reported as R_JOB_FAIL - 1 (fleetcheck does the same). */
    return fold == MESH_R_JOB_FAIL ? fold - 1u : fold;
}

static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    mesh_view v = mesh_ctx_view(ctx);
    switch (op) {
    case MESH_OP_JOB: return item(ctx, arg);
    case OP_FAIL_ITEM: mesh_view_set_u32(v, V_FAIL_ITEM, arg + 1u); return MESH_R_NONE;
    case OP_TRAP_ITEM: mesh_view_set_u32(v, V_TRAP_ITEM, arg + 1u); return MESH_R_NONE;
    case OP_CLEAR:
        mesh_view_set_u32(v, V_FAIL_ITEM, 0);
        mesh_view_set_u32(v, V_TRAP_ITEM, 0);
        return MESH_R_NONE;
    default: return MESH_R_BAD_OP;
    }
}

MESH_PROGRAM(init, task)
