/* The table-level tests of alpha-rust-mesh's mesh-abi pipe.rs, ported one for
 * one: host_push_pop_through_the_tables and hold_and_flight. */
#include "mesh/abi/pipe.h"
#include "mesh/abi/view_heap.h"
#include "mesh/check.h"
#include "mem.h"

static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

static void host_push_pop_through_the_tables(void) {
    /* Consumer slot 2 (inbox at view + 0x80, 8 x 8 bytes: two sides of 4), producer slot 5 feeding it. */
    mem *m = mem_new(MESH_MAILBOX_BYTES);
    const mesh_words *w = &m->words;
    uint8_t ones[8], r4[4];
    memset(ones, 1, 8);
    memset(r4, 1, 4);
    mem_set(m, mesh_prog_entry(2) + MESH_PT_STATE, MESH_ST_RUNNING);
    mem_set(m, mesh_prog_entry(2) + MESH_PT_VIEW_OFF, MESH_VIEW_HEAP);
    mem_set(m, mesh_pipe_entry(2) + MESH_PP_IN_OFF, 0x80);
    mem_set(m, mesh_pipe_entry(2) + MESH_PP_IN_CAP, 8);
    mem_set(m, mesh_pipe_entry(2) + MESH_PP_IN_REC, 8);
    mem_set(m, mesh_prog_entry(5) + MESH_PT_STATE, MESH_ST_RUNNING);
    CHECK_I(mesh_pipe_push(w, 5, 2, ones, 8), -MESH_PIPE_ERR_NOT_CONNECTED);
    mem_set(m, mesh_pipe_entry(5) + MESH_PP_FEEDS_MASK, 1u << 2);
    CHECK_I(mesh_pipe_push(w, 5, 2, r4, 4), -MESH_PIPE_ERR_SIZE);
    /* Host code pushes on the CPU side: 4 records fill it. */
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t rec[8];
        memset(rec, i, 8);
        CHECK_I(mesh_pipe_push(w, 5, 2, rec, 8), 1);
    }
    uint8_t nine[8];
    memset(nine, 9, 8);
    CHECK_I(mesh_pipe_push(w, 5, 2, nine, 8), 0);
    mesh_pipe_inbox inbox;
    CHECK_I(mesh_pipe_target(w, 2, 2, &inbox), 0);
    mesh_pipe_ring cpu = mesh_pipe_inbox_side(&inbox, MESH_PIPE_SIDE_CPU), gpu = mesh_pipe_inbox_side(&inbox, MESH_PIPE_SIDE_GPU);
    CHECK_U(cpu.base, MESH_VIEW_HEAP + 0x80);
    CHECK_U(mem_get(m, cpu.base + MESH_PIPE_PI_FULL), 1);
    CHECK_U(mesh_pipe_ring_len(&cpu, w), 4);
    CHECK_U(mesh_pipe_ring_len(&gpu, w), 0);
    /* A GPU producer's records go to the other side (its head is never CASed by the host's pushes). */
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t p;
        CHECK(mesh_pipe_ring_reserve_push(&gpu, w, 6, &p));
        uint32_t v[2] = {0x100 + i, 0x100 + i};
        mem_put(m, &gpu, p, v, 2);
        mesh_pipe_ring_commit_push(&gpu, w, p);
    }
    CHECK_U(mesh_pipe_inbox_len(&inbox, w), 6);
    /* The consumer drains both, the fuller side first; each side in order. */
    uint8_t out[8];
    uint32_t got[8], n = 0;
    while (n < 8 && mesh_pipe_pop(w, 2, out, 8) == 1) got[n++] = le32(out);
    CHECK_U(n, 6);
    uint32_t c[4], g[2], nc = 0, ng = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (got[i] == 0x100 || got[i] == 0x101) g[ng < 2 ? ng++ : ng] = got[i];
        else if (nc < 4) c[nc++] = got[i];
    }
    CHECK_U(nc, 4);
    CHECK_U(ng, 2);
    CHECK_U(c[0], 0);
    CHECK_U(c[1], 0x01010101u);
    CHECK_U(c[2], 0x02020202u);
    CHECK_U(c[3], 0x03030303u);
    CHECK_U(g[0], 0x100);
    CHECK_U(g[1], 0x101);
    CHECK_U(got[0], 0); /* the CPU side was fuller */
    CHECK_I(mesh_pipe_pop(w, 2, out, 8), 0);
    CHECK_I(mesh_pipe_pop(w, 5, out, 8), -MESH_PIPE_ERR_NOT_CONNECTED);
    mem_set(m, mesh_prog_entry(2) + MESH_PT_STATE, MESH_ST_EMPTY);
    CHECK_I(mesh_pipe_push(w, 5, 2, ones, 8), -MESH_PIPE_ERR_GONE);
    /* Unloaded, then the feed disconnected: still Gone (a stale slot id is no fault). */
    mem_set(m, mesh_pipe_entry(5) + MESH_PP_FEEDS_MASK, 0);
    CHECK_I(mesh_pipe_push(w, 5, 2, ones, 8), -MESH_PIPE_ERR_GONE);
    mem_free(m);
}

static void hold_and_flight(void) {
    mem *m = mem_new(MESH_MAILBOX_BYTES);
    const mesh_words *w = &m->words;
    uint8_t ones[8], r4[4], out[8];
    memset(ones, 1, 8);
    memset(r4, 1, 4);
    mem_set(m, mesh_prog_entry(2) + MESH_PT_STATE, MESH_ST_RUNNING);
    mem_set(m, mesh_prog_entry(2) + MESH_PT_VIEW_OFF, MESH_VIEW_HEAP);
    mem_set(m, mesh_pipe_entry(2) + MESH_PP_IN_OFF, 0x80);
    mem_set(m, mesh_pipe_entry(2) + MESH_PP_IN_CAP, 4);
    mem_set(m, mesh_pipe_entry(2) + MESH_PP_IN_REC, 8);
    mem_set(m, mesh_prog_entry(5) + MESH_PT_STATE, MESH_ST_RUNNING);
    mem_set(m, mesh_pipe_entry(5) + MESH_PP_FEEDS_MASK, 1u << 2);
    mem_set(m, mesh_pipe_entry(2) + MESH_PP_IN_HOLD, 1);
    CHECK_I(mesh_pipe_push(w, 5, 2, ones, 8), 0); /* held: full, not a fault */
    mesh_pipe_inbox in;
    int t = mesh_pipe_target(w, 5, 2, &in);
    CHECK_I(mesh_pipe_code(t < 0 ? t : 1), MESH_PIPE_NONE);
    mem_set(m, mesh_pipe_entry(2) + MESH_PP_IN_HOLD, 0);
    CHECK_I(mesh_pipe_push(w, 5, 2, ones, 8), 1);
    CHECK_U(mem_get(m, mesh_pipe_entry(5) + MESH_PP_FLIGHT), 0); /* every push ends its flight */
    CHECK_I(mesh_pipe_push(w, 5, 2, r4, 4), -MESH_PIPE_ERR_SIZE);
    CHECK_U(mem_get(m, mesh_pipe_entry(5) + MESH_PP_FLIGHT), 0);
    /* The epoch's parity picks the word: an odd epoch counts in PP_FLIGHT1. */
    mem_set(m, MESH_H_PIPE_EPOCH, 3);
    uint32_t parity = mesh_pipe_flight_begin(w, 5);
    CHECK_U(parity, 1);
    CHECK_U(mem_get(m, mesh_flight_word(5, 1)), 1);
    CHECK_U(mem_get(m, mesh_flight_word(5, 0)), 0);
    mem_set(m, MESH_H_PIPE_EPOCH, 4); /* meshd moved on: the push still ends in the word it counted in */
    mesh_pipe_flight_end(w, 5, parity);
    CHECK_U(mem_get(m, mesh_flight_word(5, 1)), 0);
    /* A block that keeps changing: busy, bounded. */
    mem_set(m, mesh_prog_entry(2) + MESH_PT_GEN, 1);
    CHECK_I(mesh_pipe_push(w, 5, 2, ones, 8), 0);
    CHECK_I(mesh_pipe_pop(w, 2, out, 8), 0);
    mem_free(m);
}

/* code() over every result, as the runtimes return them. */
static void codes(void) {
    CHECK_I(mesh_pipe_code(1), MESH_PIPE_OK);
    CHECK_I(mesh_pipe_code(0), MESH_PIPE_NONE);
    CHECK_I(mesh_pipe_code(-MESH_PIPE_ERR_NOT_CONNECTED), MESH_PIPE_NOT_CONNECTED);
    CHECK_I(mesh_pipe_code(-MESH_PIPE_ERR_SIZE), MESH_PIPE_SIZE);
    CHECK_I(mesh_pipe_code(-MESH_PIPE_ERR_GONE), MESH_PIPE_GONE);
    CHECK_I(mesh_pipe_code(-MESH_PIPE_ERR_BUSY), MESH_PIPE_NONE);
    CHECK_I(-MESH_PIPE_ERR_NOT_CONNECTED, MESH_PIPE_NOT_CONNECTED);
    CHECK_I(-MESH_PIPE_ERR_SIZE, MESH_PIPE_SIZE);
    CHECK_I(-MESH_PIPE_ERR_GONE, MESH_PIPE_GONE);
}

int main(void) {
    host_push_pop_through_the_tables();
    hold_and_flight();
    codes();
    return mesh_check_done();
}
