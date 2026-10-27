/* The instance's Ctx, built on first use (a wasm instance has one thread). Also memcpy, memset and
 * memmove: freestanding wasm32 code may call them (struct and array copies). */
#include "mesh/rt_wasm.h"

static mesh_ctx ctx;
static bool ready;

const mesh_ctx *mesh_ctx_get(void) {
    if (!ready) {
        ctx.slot = mesh_sys_info(MESH_WASM_I_SLOT);
        ctx.node = mesh_sys_info(MESH_WASM_I_NODE);
        ctx.nodes = mesh_sys_info(MESH_WASM_I_NODES);
        ctx.view.h = MESH_WASM_W_VIEW;
        ctx.view.base = 0;
        ctx.view.len = mesh_sys_info(MESH_WASM_I_VIEW_BYTES);
        ctx.req_off = mesh_sys_info(MESH_WASM_I_REQ_OFF);
        ctx.resp_off = mesh_sys_info(MESH_WASM_I_RESP_OFF);
        ctx.resp_max = mesh_sys_info(MESH_WASM_I_RESP_MAX);
        ctx.lockstep = mesh_sys_info(MESH_WASM_I_LOCKSTEP) != 0;
        ready = true;
    }
    return &ctx;
}

void *memcpy(void *restrict dst, const void *restrict src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

/* compiler-rt's 128-bit multiply: clang calls it for u64 x u64 -> u128 products on wasm32 (nothing
 * else here links compiler-rt). */
__extension__ typedef __int128 mesh_ti;
mesh_ti __multi3(mesh_ti a, mesh_ti b);
mesh_ti __multi3(mesh_ti a, mesh_ti b) {
    __extension__ typedef unsigned __int128 mesh_tu;
    mesh_tu x = (mesh_tu)a, y = (mesh_tu)b;
    uint64_t a0 = (uint64_t)x, a1 = (uint64_t)(x >> 64), b0 = (uint64_t)y, b1 = (uint64_t)(y >> 64);
    /* a0 * b0 in full from 32-bit halves, then the cross terms into the high word. */
    uint64_t l0 = a0 & 0xffffffffu, h0 = a0 >> 32, l1 = b0 & 0xffffffffu, h1 = b0 >> 32;
    uint64_t ll = l0 * l1, lh = l0 * h1, hl = h0 * l1, hh = h0 * h1;
    uint64_t mid = (ll >> 32) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
    uint64_t lo = (ll & 0xffffffffu) | mid << 32;
    uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32) + a0 * b1 + a1 * b0;
    return (mesh_ti)((mesh_tu)hi << 64 | lo);
}
