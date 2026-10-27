/* The host side of one program slot, for tests on the simulator. */
#if !defined(__NVPTX__)

#include "mesh/rt/sim_mailbox.h"
#include "mesh/abi.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static _Atomic uint32_t *a32(uint8_t *p) { return (_Atomic uint32_t *)(void *)p; }
static _Atomic uint64_t *a64(uint8_t *p) { return (_Atomic uint64_t *)(void *)p; }

uint32_t mesh_sim_u32(const uint8_t *p) { return atomic_load_explicit(a32((uint8_t *)(uintptr_t)p), memory_order_acquire); }
uint64_t mesh_sim_u64(const uint8_t *p) { return atomic_load_explicit(a64((uint8_t *)(uintptr_t)p), memory_order_acquire); }
float mesh_sim_f32(const uint8_t *p) {
    uint32_t u = mesh_sim_u32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
void mesh_sim_set_u32(uint8_t *p, uint32_t v) { atomic_store_explicit(a32(p), v, memory_order_release); }
void mesh_sim_set_u64(uint8_t *p, uint64_t v) { atomic_store_explicit(a64(p), v, memory_order_release); }
void mesh_sim_set_f32(uint8_t *p, float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    mesh_sim_set_u32(p, u);
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static void nap(void) {
    struct timespec ts = {0, 20000};
    nanosleep(&ts, NULL);
}

uint8_t *mesh_sim_mailbox(void) {
    uint8_t *mb = aligned_alloc(4096, MESH_MAILBOX_BYTES);
    if (!mb) return NULL;
    memset(mb, 0, MESH_MAILBOX_BYTES);
    mesh_sim_set_u32(mb + MESH_H_MAGIC, MESH_MAGIC);
    mesh_sim_set_u32(mb + MESH_H_VERSION, MESH_VERSION);
    mesh_sim_set_u32(mb + MESH_H_PEER, MESH_PEER_NONE);
    mesh_sim_set_u32(mb + MESH_H_SYSTEMS, 1);
    mesh_sim_set_u32(mb + MESH_H_SMS, 1);
    return mb;
}

void mesh_sim_mailbox_free(uint8_t *mb) { free(mb); }

uint8_t *mesh_sim_ctl(uint8_t *mb, uint32_t slot) { return mb + mesh_prog_ctl(slot); }
uint8_t *mesh_sim_view(uint8_t *mb, uint32_t slot) {
    return mb + mesh_sim_u32(mb + mesh_prog_entry(slot) + MESH_PT_VIEW_OFF);
}

/* The entry's writer side of its seqlock: gen odd while it changes. */
static void gen_begin(uint8_t *e) { mesh_sim_set_u32(e + MESH_PT_GEN, mesh_sim_u32(e + MESH_PT_GEN) | 1u); }
static void gen_end(uint8_t *e) { mesh_sim_set_u32(e + MESH_PT_GEN, (mesh_sim_u32(e + MESH_PT_GEN) | 1u) + 1u); }

uint8_t *mesh_sim_install(uint8_t *mb, uint32_t slot, const mesh_sim_prog *p) {
    uint8_t *e = mb + mesh_prog_entry(slot);
    uint32_t view = p->view_off ? p->view_off : MESH_VIEW_HEAP;
    if ((uint64_t)view + p->view_bytes > MESH_MAILBOX_BYTES) {
        fprintf(stderr, "mesh-rt-sim-mailbox: a %u-byte view at 0x%x does not fit the mailbox\n", p->view_bytes, view);
        abort();
    }
    gen_begin(e);
    memset(e + MESH_PT_NAME, 0, MESH_NAME_BYTES);
    size_t n = strlen(p->name);
    memcpy(e + MESH_PT_NAME, p->name, n < MESH_NAME_MAX ? n : MESH_NAME_MAX);
    mesh_sim_set_u32(e + MESH_PT_VIEW_OFF, p->view_bytes ? view : 0);
    mesh_sim_set_u32(e + MESH_PT_VIEW_BYTES, p->view_bytes);
    mesh_sim_set_u32(e + MESH_PT_NODES, p->nodes ? p->nodes : 1);
    mesh_sim_set_u32(e + MESH_PT_WARPS, p->warps ? p->warps : 1);
    mesh_sim_set_u32(e + MESH_PT_PEER, p->peer_mode);
    mesh_sim_set_u32(e + MESH_PT_READS_MASK, p->reads_mask);
    mesh_sim_set_u32(e + MESH_PT_INPUT_OFF, p->input_off);
    mesh_sim_set_u32(e + MESH_PT_INPUT_LEN, p->input_len);
    mesh_sim_set_u32(e + MESH_PT_REQ_OFF, p->req_off);
    mesh_sim_set_u32(e + MESH_PT_REQ_MAX, p->req_max);
    mesh_sim_set_u32(e + MESH_PT_RESP_OFF, p->resp_off);
    mesh_sim_set_u32(e + MESH_PT_RESP_MAX, p->resp_max);
    mesh_sim_set_u32(e + MESH_PT_RT_ABI, MESH_RT_ABI);
    mesh_sim_set_u32(e + MESH_PT_STATE, MESH_ST_RUNNING);
    uint8_t *x = mb + mesh_prog_ext(slot);
    mesh_sim_set_u32(x + MESH_PX_PARAM, p->param);
    mesh_sim_set_u64(x + MESH_PX_SCRATCH, (uint64_t)(uintptr_t)p->scratch);
    mesh_sim_set_u32(x + MESH_PX_SCRATCH_BYTES, p->scratch ? p->scratch_bytes : 0);
    gen_end(e);
    return mb + view;
}

void mesh_sim_set_state(uint8_t *mb, uint32_t slot, uint32_t state) {
    uint8_t *e = mb + mesh_prog_entry(slot);
    gen_begin(e);
    mesh_sim_set_u32(e + MESH_PT_STATE, state);
    gen_end(e);
}

mesh_sim_launch *mesh_sim_launch_prog(uint8_t *mb, uint32_t slot, const mesh_sim_prog *p, mesh_sim_kernel kernel) {
    mesh_sim_launch *l = mesh_sim_start(kernel, mb, slot, p->nodes ? p->nodes : 1, p->warps ? p->warps : 1, p->shared_bytes);
    if (!l) {
        fprintf(stderr, "mesh-rt-sim-mailbox: could not launch %s\n", p->name);
        abort();
    }
    return l;
}

/* The next ticket to publish lives in P_RING_TAIL (meshd's informational copy). */
uint32_t mesh_sim_publish(uint8_t *mb, uint32_t slot, uint32_t op, uint32_t arg) {
    uint8_t *ctl = mesh_sim_ctl(mb, slot);
    uint32_t t = (uint32_t)mesh_sim_u64(ctl + MESH_P_RING_TAIL);
    /* Ticket t may be published only once ticket t - 64 completed (its completion slot is reused). */
    if (t >= MESH_PROG_RING) {
        uint32_t old = t - MESH_PROG_RING;
        while (mesh_sim_u32(ctl + MESH_P_MARKS + 4 * (old % MESH_PROG_RESULTS)) != old + 1) nap();
    }
    uint8_t *e = ctl + MESH_P_RING + MESH_ENTRY_BYTES * (t % MESH_PROG_RING);
    mesh_sim_set_u32(e + MESH_E_OP, op);
    mesh_sim_set_u32(e + MESH_E_ARG, arg);
    mesh_sim_set_u32(e + MESH_E_SLOT, t % MESH_PROG_RESULTS);
    mesh_sim_set_u32(e + MESH_E_SEQ, t + 1);
    mesh_sim_set_u64(ctl + MESH_P_RING_TAIL, (uint64_t)t + 1);
    return t;
}

bool mesh_sim_wait(uint8_t *mb, uint32_t slot, uint32_t t, uint32_t *result, uint32_t timeout_ms) {
    uint8_t *ctl = mesh_sim_ctl(mb, slot);
    uint32_t c = t % MESH_PROG_RESULTS;
    uint64_t until = now_ms() + timeout_ms;
    while (mesh_sim_u32(ctl + MESH_P_MARKS + 4 * c) != t + 1) {
        if (now_ms() > until) return false;
        nap();
    }
    if (result) *result = mesh_sim_u32(ctl + MESH_P_RESULTS + 4 * c);
    return true;
}

uint32_t mesh_sim_call(uint8_t *mb, uint32_t slot, uint32_t op, uint32_t arg) {
    uint32_t t = mesh_sim_publish(mb, slot, op, arg), r = 0;
    if (!mesh_sim_wait(mb, slot, t, &r, 60000)) {
        fprintf(stderr, "mesh-rt-sim-mailbox: ticket %u (op %u, arg %u) of slot %u never completed\n", t, op, arg, slot);
        abort();
    }
    return r;
}

uint32_t mesh_sim_request(uint8_t *mb, uint32_t slot, const void *req, uint32_t len, void *out, uint32_t cap, uint32_t *out_len) {
    uint8_t *e = mb + mesh_prog_entry(slot), *ctl = mesh_sim_ctl(mb, slot), *view = mesh_sim_view(mb, slot);
    uint32_t req_off = mesh_sim_u32(e + MESH_PT_REQ_OFF), req_max = mesh_sim_u32(e + MESH_PT_REQ_MAX);
    uint32_t resp_off = mesh_sim_u32(e + MESH_PT_RESP_OFF);
    if (len > req_max) len = req_max;
    memcpy(view + req_off, req, len);
    mesh_sim_set_u32(ctl + MESH_P_REQ_LEN, len);
    uint64_t seq = mesh_sim_u64(ctl + MESH_P_REQ_SEQ) + 1;
    mesh_sim_set_u64(ctl + MESH_P_REQ_SEQ, seq);
    uint32_t r = mesh_sim_call(mb, slot, MESH_OP_REQ, 0);
    if (mesh_sim_u64(ctl + MESH_P_RESP_SEQ) != seq) {
        *out_len = UINT32_MAX;
        return r;
    }
    uint32_t n = mesh_sim_u32(ctl + MESH_P_RESP_LEN);
    memcpy(out, view + resp_off, n < cap ? n : cap);
    *out_len = n;
    return r;
}

bool mesh_sim_input(uint8_t *mb, uint32_t slot, const void *bytes, uint32_t n) {
    uint8_t *e = mb + mesh_prog_entry(slot);
    uint32_t off = mesh_sim_u32(e + MESH_PT_INPUT_OFF), len = mesh_sim_u32(e + MESH_PT_INPUT_LEN);
    if (len == 0) return false;
    uint8_t *q = mesh_sim_view(mb, slot) + off;
    uint32_t head = mesh_sim_u32(q + MESH_IQ_HEAD), tail = mesh_sim_u32(q + MESH_IQ_TAIL);
    if (head - tail + n > len) return false;
    for (uint32_t i = 0; i < n; i++) q[MESH_IQ_DATA + ((head + i) & (len - 1))] = ((const uint8_t *)bytes)[i];
    mesh_sim_set_u32(q + MESH_IQ_HEAD, head + n);
    return true;
}

void mesh_sim_halt(uint8_t *mb, uint32_t slot, mesh_sim_launch *l) {
    uint8_t *ctl = mesh_sim_ctl(mb, slot);
    mesh_sim_set_u32(ctl + MESH_P_SHUTDOWN, MESH_SHUTDOWN_HALT);
    mesh_sim_join(l);
    mesh_sim_set_u32(ctl + MESH_P_RING_HEAD, (uint32_t)mesh_sim_u64(ctl + MESH_P_RING_TAIL));
    mesh_sim_set_u32(ctl + MESH_P_ONLINE, 0);
    mesh_sim_set_u32(ctl + MESH_P_HALTED, 0);
    mesh_sim_set_u32(ctl + MESH_P_SHUTDOWN, 0);
}

#else
typedef int mesh_rt_sim_mailbox_unused;
#endif
