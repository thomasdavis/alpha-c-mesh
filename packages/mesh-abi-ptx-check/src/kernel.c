/* A kernel that uses the mesh contract on the GPU: a pipe push through the
 * words interface (device atomics), the program table's names and the peer
 * frame kinds, so every @ajax/mesh-abi-* package is compiled, linked and
 * code-generated for nvptx64. Not a program meshd runs; a build check. */
#include "mesh/abi.h"

#include <stdatomic.h>

static _Atomic uint32_t *at(void *c, size_t a) { return (_Atomic uint32_t *)((uint8_t *)c + a); }
static uint32_t ld(void *c, size_t a) { return atomic_load_explicit(at(c, a), memory_order_acquire); }
static void st(void *c, size_t a, uint32_t v) { atomic_store_explicit(at(c, a), v, memory_order_release); }
static uint32_t cas(void *c, size_t a, uint32_t cmp, uint32_t desired) {
    atomic_compare_exchange_strong_explicit(at(c, a), &cmp, desired, memory_order_acq_rel, memory_order_acquire);
    return cmp;
}
static uint32_t add(void *c, size_t a, uint32_t v) { return atomic_fetch_add_explicit(at(c, a), v, memory_order_acq_rel); }
static void rd(void *c, size_t a, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) out[i] = ((uint8_t *)c)[a + i];
}
static void wr(void *c, size_t a, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) ((uint8_t *)c)[a + i] = b[i];
}

__attribute__((nvptx_kernel)) void mesh_abi_check(uint8_t *mb, uint32_t slot) {
    mesh_words w = {mb, ld, st, cas, add, NULL, rd, wr};
    uint8_t rec[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    int r = mesh_pipe_push(&w, slot, 0, rec, sizeof rec);
    const char *state = MESH_STATES[slot % MESH_STATES_LEN];
    uint32_t v = (uint32_t)r + (uint32_t)state[0] + (uint32_t)mesh_frame_kind((uint8_t)slot) +
                 (uint32_t)mesh_fnv1a64(rec, sizeof rec) + mesh_nic_virtual((const uint8_t *)"lo", 2) +
                 mesh_pipe_type_hash((const uint8_t *)"msg", 3) + mesh_doom_map_cell(slot % 16, 3);
    st(mb, mesh_prog_ctl(slot) + MESH_P_COUNTERS, v);
}
