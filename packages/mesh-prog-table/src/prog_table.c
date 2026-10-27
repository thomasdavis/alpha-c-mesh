/* The program table writer: the PT_GEN seqlock around every rewrite. */
#include "mesh/prog_table.h"

#include <stdatomic.h>
#include <string.h>

static uint32_t word(const uint8_t *b, size_t at) {
    return (uint32_t)b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24;
}

static uint32_t gen_odd(mesh_mailbox mb, size_t e) {
    uint32_t gen = mesh_mailbox_get(mb, e + MESH_PT_GEN) | 1u;
    mesh_mailbox_set(mb, e + MESH_PT_GEN, gen);
    atomic_thread_fence(memory_order_seq_cst);
    return gen;
}

void mesh_pt_write_entry(mesh_mailbox mb, uint32_t s, const uint8_t entry[MESH_PT_BYTES], const uint8_t ext[MESH_PX_BYTES],
                         uint32_t state) {
    size_t e = mesh_prog_entry(s);
    uint32_t gen = gen_odd(mb, e);
    for (size_t at = MESH_PT_NAME; at < MESH_PT_BYTES; at += 4) mesh_mailbox_set(mb, e + at, word(entry, at));
    for (size_t at = 0; at < MESH_PX_BYTES; at += 4) mesh_mailbox_set(mb, mesh_prog_ext(s) + at, word(ext, at));
    mesh_mailbox_set(mb, e + MESH_PT_STATE, state);
    mesh_mailbox_publish(mb, e + MESH_PT_GEN, gen + 1);
    mesh_mailbox_fetch_add(mb, MESH_H_PROG_SEQ, 1);
}

void mesh_pt_set_state(mesh_mailbox mb, uint32_t s, uint32_t state) {
    if (mesh_mailbox_get(mb, mesh_prog_entry(s) + MESH_PT_STATE) == state) return;
    mesh_mailbox_publish(mb, mesh_prog_entry(s) + MESH_PT_STATE, state);
    mesh_mailbox_fetch_add(mb, MESH_H_PROG_SEQ, 1);
}

void mesh_pt_clear(mesh_mailbox mb, uint32_t s) {
    static const uint8_t zero_e[MESH_PT_BYTES], zero_x[MESH_PX_BYTES], zero_p[MESH_PP_BYTES];
    mesh_pt_write_entry(mb, s, zero_e, zero_x, MESH_ST_EMPTY);
    mesh_pt_write_pipe(mb, s, zero_p);
}

static int ours(size_t i) { return 4 * i != MESH_PP_FLIGHT && 4 * i != MESH_PP_FLIGHT1; }

void mesh_pt_write_pipe(mesh_mailbox mb, uint32_t s, const uint8_t b[MESH_PP_BYTES]) {
    size_t e = mesh_prog_entry(s), p = mesh_pipe_entry(s);
    int same = 1;
    for (size_t i = 0; i < MESH_PP_BYTES / 4 && same; i++)
        if (ours(i) && mesh_mailbox_get(mb, p + 4 * i) != word(b, 4 * i)) same = 0;
    if (same) return;
    uint32_t gen = gen_odd(mb, e);
    for (size_t i = 0; i < MESH_PP_BYTES / 4; i++) if (ours(i)) mesh_mailbox_set(mb, p + 4 * i, word(b, 4 * i));
    mesh_mailbox_publish(mb, e + MESH_PT_GEN, gen + 1);
}

void mesh_pt_set_hold(mesh_mailbox mb, uint32_t s, bool on) {
    size_t e = mesh_prog_entry(s);
    uint32_t gen = gen_odd(mb, e);
    mesh_mailbox_set(mb, mesh_pipe_entry(s) + MESH_PP_IN_HOLD, on);
    mesh_mailbox_publish(mb, e + MESH_PT_GEN, gen + 1);
    atomic_thread_fence(memory_order_seq_cst);
}

uint32_t mesh_pt_flight(mesh_mailbox mb, uint32_t s, uint32_t parity) { return mesh_mailbox_get(mb, mesh_flight_word(s, parity)); }

uint32_t mesh_pt_flight_epoch(mesh_mailbox mb) {
    uint32_t old = mesh_mailbox_get(mb, MESH_H_PIPE_EPOCH);
    mesh_mailbox_publish(mb, MESH_H_PIPE_EPOCH, old + 1);
    atomic_thread_fence(memory_order_seq_cst);
    return old & 1u;
}

void mesh_pt_flight_clear(mesh_mailbox mb, uint32_t s) {
    mesh_mailbox_set(mb, mesh_flight_word(s, 0), 0);
    mesh_mailbox_set(mb, mesh_flight_word(s, 1), 0);
}

void mesh_pt_reset_slot(mesh_mailbox mb, uint32_t s) {
    mesh_mailbox_zero(mb, mesh_prog_ctl(s), MESH_P_RUN_REQ);
    mesh_mailbox_zero(mb, mesh_prog_ctl(s) + MESH_P_RUN_REQ + 4, MESH_PROG_CTL_BYTES - MESH_P_RUN_REQ - 4);
    mesh_mailbox_zero(mb, mesh_node_rec(s, 0), MESH_PROG_NODES_BYTES);
    mesh_mailbox_zero(mb, mesh_node_load(s, 0), (size_t)MESH_NL_STRIDE * MESH_PROG_MAX_NODES);
    mesh_mailbox_zero(mb, mesh_trace_rec(s, 0), (size_t)MESH_TRACE_BYTES * MESH_TRACE_PER_PROG);
    mesh_pt_flight_clear(mb, s);
}

void mesh_pt_name_of(mesh_mailbox mb, uint32_t s, char out[MESH_NAME_BYTES + 1]) {
    mesh_mailbox_read(mb, mesh_prog_entry(s) + MESH_PT_NAME, (uint8_t *)out, MESH_NAME_BYTES);
    out[MESH_NAME_BYTES] = 0;
}

bool mesh_pt_find(mesh_mailbox mb, const char *name, uint32_t *slot) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (mesh_mailbox_get(mb, mesh_prog_entry(s) + MESH_PT_STATE) == MESH_ST_EMPTY) continue;
        char n[MESH_NAME_BYTES + 1];
        mesh_pt_name_of(mb, s, n);
        if (strcmp(n, name) == 0) { *slot = s; return true; }
    }
    return false;
}

void mesh_pt_read_entry(mesh_mailbox mb, uint32_t s, uint8_t out[MESH_PT_BYTES]) {
    size_t e = mesh_prog_entry(s);
    for (;;) {
        uint32_t g = mesh_mailbox_get(mb, e + MESH_PT_GEN);
        if ((g & 1u) == 0) {
            mesh_mailbox_read(mb, e, out, MESH_PT_BYTES);
            atomic_thread_fence(memory_order_seq_cst);
            if (mesh_mailbox_get(mb, e + MESH_PT_GEN) == g) return;
        }
    }
}
