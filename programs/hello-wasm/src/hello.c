/* hello for the wasm executor (alpha-rust-mesh programs/hello/wasm): op 0 = arg * 3 + 7.
 * View: 0x0 magic "HELO" (set by init), 0x4 last arg, 0x8 last result. Counters: 0 tasks, 1 inits.
 *
 * wasm-only test hooks (kcheck W*, lifecheck; the GPU images answer R_BAD_OP): op 8 `arg` u32 loads
 * of the view, one mesh::ld32 call each (the per-access cost); op 9 `arg` bulk reads of the whole
 * 4 KiB view into linear memory, one mesh::read call each; op 10 traps (unreachable); op 11 spins
 * until meshd interrupts it (task_ms); op 12 grows linear memory by `arg` MiB (the old size in
 * pages, or u32::MAX past mem_mb). */
#include "mesh/rt_wasm.h"

#define MAGIC 0x4f4c4548u /* "HELO" */

static void init(const mesh_ctx *ctx) {
    mesh_ctx_set_u32(ctx, 0x0, MAGIC);
    mesh_ctx_count(ctx, 1, 1);
}

static uint8_t page[4096];

static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    switch (op) {
    case MESH_OP_TASK: {
        uint32_t r = arg * 3u + 7u;
        mesh_ctx_set_u32(ctx, 0x4, arg);
        mesh_ctx_set_u32(ctx, 0x8, r);
        mesh_ctx_count(ctx, 0, 1);
        return r;
    }
    case 8: {
        mesh_view v = mesh_ctx_view(ctx);
        uint32_t x = 0;
        for (uint32_t i = 0; i < arg; i++) x += mesh_view_u32(v, (i % 1024) * 4);
        return x;
    }
    case 9: {
        mesh_view v = mesh_ctx_view(ctx);
        uint32_t x = 0;
        for (uint32_t i = 0; i < arg; i++) {
            mesh_view_read(v, 0, page, sizeof page);
            x ^= page[0];
        }
        return x;
    }
    case 10: MESH_TRAP();
    case 11:
        for (;;) {
            /* spin until interrupted */
            __asm__ volatile("" ::: "memory");
        }
    case 12: return (uint32_t)__builtin_wasm_memory_grow(0, (unsigned long)arg * 16);
    default: return MESH_R_BAD_OP;
    }
}

MESH_PROGRAM(init, task)
