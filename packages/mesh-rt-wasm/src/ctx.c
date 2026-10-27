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
