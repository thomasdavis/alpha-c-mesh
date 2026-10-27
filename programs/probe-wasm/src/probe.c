/* probe: one op per mesh::* import (mesh-abi v2::wasm), for the wasm executor's tests. The view is
 * 0x100 bytes: a request window at 0x40 (0x20 bytes), the response at 0x60 (0x20), an input queue
 * at 0x80 (16 bytes). Each op's result is what the import returned (or a fold of it). */
#include "mesh/rt_wasm.h"

static void init(const mesh_ctx *ctx) { (void)ctx; }

static uint8_t buf[64];

static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    mesh_view v = mesh_ctx_view(ctx);
    switch (op) {
    case 17: return mesh_sys_ld32(MESH_WASM_W_VIEW, arg); /* raw: no guest-side check */
    case 3: return (uint32_t)mesh_sys_peer(arg);
    case 4: return (uint32_t)mesh_sys_find("probe", 5);
    case 5: mesh_sys_respond(arg); return 0;
    case 6: {
        uint32_t n = mesh_sys_input_pending(), s = 0;
        int32_t b;
        while ((b = mesh_sys_input_pop()) >= 0) s = s * 31 + (uint32_t)b;
        return n << 24 | (s & 0xffffff);
    }
    case 7: return mesh_sys_exchange(1);
    case 8: mesh_sys_read(MESH_WASM_W_VIEW, 0, (void *)(uintptr_t)0xfffffff0u, 16); return 0;
    case 9: return mesh_sys_prog_state(arg);
    case 10: mesh_sys_count(1, arg); return mesh_sys_counter(1);
    case 11: mesh_sys_st8(MESH_WASM_W_VIEW, arg, 0xab); return mesh_sys_ld8(MESH_WASM_W_VIEW, arg);
    case 12: {
        uint32_t seen = mesh_view_cas32(v, 0x10, 0, 7);
        uint32_t old = mesh_view_add32(v, 0x10, 5);
        uint64_t old64 = mesh_view_add64(v, 0x18, (uint64_t)1 << 40);
        return seen * 100 + old + (uint32_t)(old64 >> 40) + mesh_view_u32(v, 0x10) * 1000;
    }
    case 13: return (uint32_t)mesh_sys_ld64(MESH_WASM_W_VIEW, 4); /* misaligned */
    case 14: return mesh_sys_info(arg);
    case 15: {
        for (int i = 0; i < 64; i++) buf[i] = (uint8_t)(i + arg);
        mesh_view_write(v, 0x20, buf, 32);
        mesh_view_read(v, 0x20, buf + 32, 32);
        uint32_t s = 0;
        for (int i = 0; i < 32; i++) s += buf[32 + i] == buf[i];
        return s;
    }
    case MESH_OP_REQ: { /* the request window back, upper-cased, as the response */
        mesh_view rq = mesh_ctx_request(ctx), rs = mesh_ctx_response(ctx);
        uint32_t n = mesh_view_len(rq);
        for (uint32_t i = 0; i < n; i++) {
            uint8_t c = mesh_view_u8(rq, i);
            mesh_view_set_u8(rs, i, (uint8_t)(c >= 'a' && c <= 'z' ? c - 32 : c));
        }
        mesh_ctx_respond(ctx, n);
        return n;
    }
    default: return MESH_R_BAD_OP;
    }
}

MESH_PROGRAM(init, task)
