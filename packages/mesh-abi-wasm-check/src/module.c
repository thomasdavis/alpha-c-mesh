/* A wasm module that uses the mesh contract: the guest-side constants and
 * helpers, the tables in src/ (linked for wasm32), and a pipe ring over its
 * own linear memory. Not a program meshd runs; a build check. */
#include "mesh/abi.h"

static uint8_t mem[1024];
static uint32_t ld(void *c, size_t a) { return *(uint32_t *)((uint8_t *)c + a); }
static void st(void *c, size_t a, uint32_t v) { *(uint32_t *)((uint8_t *)c + a) = v; }
static uint32_t cas(void *c, size_t a, uint32_t cmp, uint32_t desired) {
    uint32_t *p = (uint32_t *)((uint8_t *)c + a), old = *p;
    if (old == cmp) *p = desired;
    return old;
}
static uint32_t add(void *c, size_t a, uint32_t v) {
    uint32_t *p = (uint32_t *)((uint8_t *)c + a), old = *p;
    *p = old + v;
    return old;
}

/* Pushes one position through a ring of 4 in linear memory and returns what
 * a few helpers compute, so none of it is optimised away. */
uint32_t mesh_abi_check(uint32_t x) {
    mesh_words w = {mem, ld, st, cas, add, NULL, NULL, NULL};
    mesh_pipe_ring r = {0, 4, 8};
    uint32_t p = 0;
    if (mesh_pipe_ring_reserve_push(&r, &w, x, &p)) mesh_pipe_ring_commit_push(&r, &w, p);
    return p + mesh_wasm_w_peer(x) + (uint32_t)MESH_LOAD_REASONS[x % MESH_LOAD_REASONS_LEN][0] +
           mesh_backend_word(MESH_BACKEND_WASM, x) + mesh_pipe_ring_len(&r, &w);
}
