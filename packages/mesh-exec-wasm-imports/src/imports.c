/* LaneData and the mesh::* imports (mesh-host/src/wasm.rs `LaneData`, `linker`). */
#include "mesh/exec_wasm_imports.h"

#include "mesh/abi/control_block.h"
#include "mesh/abi/exchange.h"
#include "mesh/abi/ext.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/header.h"
#include "mesh/abi/input_queue.h"
#include "mesh/abi/pipe.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/wasm.h"
#include "mesh/lease.h"

#include <string.h>

static size_t ctl(const mesh_wasm_lane_data *d, size_t off) { return mesh_prog_ctl(d->slot) + off; }

void mesh_wasm_lane_data_init(mesh_wasm_lane_data *d, mesh_mailbox mb, uint32_t slot, uint32_t node, uint32_t nodes,
                              uint32_t task_ms, const _Atomic uint64_t *epoch) {
    memset(d, 0, sizeof *d);
    size_t e = mesh_prog_entry(slot);
    d->mb = mb;
    d->slot = slot;
    d->node = node;
    d->nodes = nodes;
    d->view_off = mesh_mailbox_get(mb, e + MESH_PT_VIEW_OFF);
    d->view_len = mesh_mailbox_get(mb, e + MESH_PT_VIEW_BYTES);
    d->req_off = mesh_mailbox_get(mb, e + MESH_PT_REQ_OFF);
    d->req_max = mesh_mailbox_get(mb, e + MESH_PT_REQ_MAX);
    d->resp_off = mesh_mailbox_get(mb, e + MESH_PT_RESP_OFF);
    d->resp_max = mesh_mailbox_get(mb, e + MESH_PT_RESP_MAX);
    d->input_off = mesh_mailbox_get(mb, e + MESH_PT_INPUT_OFF);
    d->input_len = mesh_mailbox_get(mb, e + MESH_PT_INPUT_LEN);
    d->lockstep = mesh_mailbox_get(mb, e + MESH_PT_PEER) == MESH_PEER_MODE_LOCKSTEP;
    d->ticks = task_ms;
    d->epoch = epoch;
}

void mesh_wasm_lane_data_task(mesh_wasm_lane_data *d, uint32_t ticket) {
    d->ticket = ticket;
    memset(d->peer_set, 0, sizeof d->peer_set);
}

void mesh_wasm_lane_fault(const mesh_wasm_lane_data *d, uint32_t code) {
    if (mesh_mailbox_get(d->mb, ctl(d, MESH_P_FAULT)) == 0) mesh_mailbox_cas(d->mb, ctl(d, MESH_P_FAULT), 0, code);
}

static bool window(const mesh_wasm_lane_data *d, uint32_t h, size_t *base, uint32_t *len) {
    if (h == MESH_WASM_W_VIEW) {
        *base = d->view_off;
        *len = d->view_len;
        return true;
    }
    if (h == MESH_WASM_W_EXCHANGE && d->lockstep) {
        *base = MESH_EX_BASE;
        *len = MESH_EX_BYTES;
        return true;
    }
    if (h >= MESH_WASM_W_PEER && h < MESH_WASM_W_PEER + MESH_PROG_SLOTS && d->peer_set[h - MESH_WASM_W_PEER]) {
        *base = d->peer_off[h - MESH_WASM_W_PEER];
        *len = d->peer_len[h - MESH_WASM_W_PEER];
        return true;
    }
    return false;
}

bool mesh_wasm_lane_at(const mesh_wasm_lane_data *d, uint32_t h, uint32_t off, uint32_t n, size_t align, size_t *at) {
    size_t base;
    uint32_t len;
    if (!window(d, h, &base, &len)) {
        mesh_wasm_lane_fault(d, h >= MESH_WASM_W_PEER ? MESH_FAULT_PEER_VIEW | (h - MESH_WASM_W_PEER) << 8
                                                      : MESH_FAULT_VIEW_BOUNDS | off << 8);
        return false;
    }
    size_t a = base + off;
    if ((uint64_t)off + n > len || a % align != 0) {
        mesh_wasm_lane_fault(d, MESH_FAULT_VIEW_BOUNDS | off << 8);
        return false;
    }
    *at = a;
    return true;
}

uint32_t mesh_wasm_lane_reads_mask(const mesh_wasm_lane_data *d) {
    size_t e = mesh_prog_entry(d->slot);
    for (;;) {
        uint32_t g = mesh_mailbox_get(d->mb, e + MESH_PT_GEN);
        if (g & 1) continue;
        uint32_t m = mesh_mailbox_get(d->mb, e + MESH_PT_READS_MASK);
        atomic_thread_fence(memory_order_seq_cst);
        if (mesh_mailbox_get(d->mb, e + MESH_PT_GEN) == g) return m;
    }
}

int32_t mesh_wasm_lane_peer(mesh_wasm_lane_data *d, uint32_t s) {
    if (s >= MESH_PROG_SLOTS) return -1;
    if ((mesh_wasm_lane_reads_mask(d) & (1u << s)) == 0) {
        mesh_wasm_lane_fault(d, MESH_FAULT_PEER_VIEW | s << 8);
        return -1;
    }
    size_t e = mesh_prog_entry(s);
    uint32_t st, off, len;
    for (;;) {
        uint32_t g = mesh_mailbox_get(d->mb, e + MESH_PT_GEN);
        if (g & 1) continue;
        st = mesh_mailbox_get(d->mb, e + MESH_PT_STATE);
        off = mesh_mailbox_get(d->mb, e + MESH_PT_VIEW_OFF);
        len = mesh_mailbox_get(d->mb, e + MESH_PT_VIEW_BYTES);
        atomic_thread_fence(memory_order_seq_cst);
        if (mesh_mailbox_get(d->mb, e + MESH_PT_GEN) == g) break;
    }
    if (st == MESH_ST_EMPTY || st == MESH_ST_FAILED) {
        d->peer_set[s] = false;
        return -1;
    }
    d->peer_set[s] = true;
    d->peer_off[s] = off;
    d->peer_len[s] = len;
    return (int32_t)len;
}

void mesh_wasm_copy_out(mesh_mailbox mb, size_t a, uint8_t *dst, size_t n) {
    if (a % 4 == 0 && n % 4 == 0) {
        for (size_t i = 0; i < n / 4; i++) {
            uint32_t w = mesh_mailbox_get(mb, a + 4 * i);
            memcpy(dst + 4 * i, &w, 4); /* little-endian hosts only, as the mailbox */
        }
    } else {
        for (size_t i = 0; i < n; i++) dst[i] = mesh_mailbox_get8(mb, a + i);
    }
}

void mesh_wasm_copy_in(mesh_mailbox mb, size_t a, const uint8_t *src, size_t n) {
    if (a % 4 == 0 && n % 4 == 0) {
        for (size_t i = 0; i < n / 4; i++) {
            uint32_t w;
            memcpy(&w, src + 4 * i, 4);
            mesh_mailbox_publish(mb, a + 4 * i, w);
        }
    } else {
        for (size_t i = 0; i < n; i++) mesh_mailbox_set8(mb, a + i, src[i]);
    }
}

/* ---- the imports ------------------------------------------------------------------------ */

#define D mesh_wasm_lane_data *d = wasm_instance_user(inst)
#define A(i) ((uint32_t)io[i])
#define UNUSED (void)ctx

/* Linear memory [ptr, ptr + len) of the instance, or NULL. */
static uint8_t *span(wasm_instance *inst, uint32_t ptr, uint32_t len) {
    uint64_t size;
    uint8_t *mem = wasm_memory(inst, &size);
    if (!mem || (uint64_t)ptr + len > size) return NULL;
    return mem + ptr;
}

static int info(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    mesh_mailbox mb = d->mb;
    uint32_t v;
    switch (A(0)) {
    case MESH_WASM_I_SLOT: v = d->slot; break;
    case MESH_WASM_I_NODE: v = d->node; break;
    case MESH_WASM_I_NODES: v = d->nodes; break;
    case MESH_WASM_I_TICKET: v = d->ticket; break;
    case MESH_WASM_I_SYSTEM: v = mesh_mailbox_get(mb, MESH_H_PEER); break;
    case MESH_WASM_I_SYSTEMS: v = mesh_mailbox_get(mb, MESH_H_SYSTEMS); break;
    case MESH_WASM_I_GROUP_RANK: v = mesh_mailbox_get(mb, MESH_H_GROUP_RANK); break;
    case MESH_WASM_I_GROUP_N: v = mesh_mailbox_get(mb, MESH_H_GROUP_N); break;
    case MESH_WASM_I_PARAM: v = mesh_mailbox_get(mb, mesh_prog_ext(d->slot) + MESH_PX_PARAM); break;
    case MESH_WASM_I_SMS: v = mesh_mailbox_get(mb, MESH_H_SMS); break;
    case MESH_WASM_I_VIEW_BYTES: v = d->view_len; break;
    case MESH_WASM_I_REQ_LEN: {
        uint32_t n = mesh_mailbox_get(mb, ctl(d, MESH_P_REQ_LEN));
        v = n < d->req_max ? n : d->req_max;
        break;
    }
    case MESH_WASM_I_REQ_OFF: v = d->req_off; break;
    case MESH_WASM_I_RESP_OFF: v = d->resp_off; break;
    case MESH_WASM_I_RESP_MAX: v = d->resp_max; break;
    case MESH_WASM_I_INPUT_LEN: v = d->input_len; break;
    case MESH_WASM_I_LOCKSTEP: v = d->lockstep; break;
    case MESH_WASM_I_READS_MASK: v = mesh_wasm_lane_reads_mask(d); break;
    default: v = 0; break;
    }
    io[0] = v;
    return WASM_NUM_OK;
}

static int ld8(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    size_t a;
    io[0] = mesh_wasm_lane_at(d, A(0), A(1), 1, 1, &a) ? mesh_mailbox_get8(d->mb, a) : 0;
    return WASM_NUM_OK;
}

static int ld32(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    size_t a;
    io[0] = mesh_wasm_lane_at(d, A(0), A(1), 4, 4, &a) ? mesh_mailbox_get(d->mb, a) : 0;
    return WASM_NUM_OK;
}

static int ld64(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    size_t a;
    io[0] = mesh_wasm_lane_at(d, A(0), A(1), 8, 8, &a) ? mesh_mailbox_get64(d->mb, a) : 0;
    return WASM_NUM_OK;
}

static int st8(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    size_t a;
    if (mesh_wasm_lane_at(d, A(0), A(1), 1, 1, &a)) mesh_mailbox_set8(d->mb, a, (uint8_t)A(2));
    return WASM_NUM_OK;
}

static int st32(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    size_t a;
    if (mesh_wasm_lane_at(d, A(0), A(1), 4, 4, &a)) mesh_mailbox_publish(d->mb, a, A(2));
    return WASM_NUM_OK;
}

static int st64(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    size_t a;
    if (mesh_wasm_lane_at(d, A(0), A(1), 8, 8, &a)) mesh_mailbox_publish64(d->mb, a, io[2]);
    return WASM_NUM_OK;
}

static int cas32(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    size_t a;
    io[0] = mesh_wasm_lane_at(d, A(0), A(1), 4, 4, &a) ? mesh_mailbox_cas(d->mb, a, A(2), A(3)) : 0;
    return WASM_NUM_OK;
}

static int add32(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    size_t a;
    io[0] = mesh_wasm_lane_at(d, A(0), A(1), 4, 4, &a) ? mesh_mailbox_fetch_add(d->mb, a, A(2)) : 0;
    return WASM_NUM_OK;
}

static int add64(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    size_t a;
    io[0] = mesh_wasm_lane_at(d, A(0), A(1), 8, 8, &a) ? mesh_mailbox_fetch_add64(d->mb, a, io[2]) : 0;
    return WASM_NUM_OK;
}

static int read_(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    uint64_t size;
    if (!wasm_memory(inst, &size)) return WASM_NUM_OK;
    size_t a;
    if (!mesh_wasm_lane_at(d, A(0), A(1), A(3), 1, &a)) return WASM_NUM_OK;
    uint8_t *dst = span(inst, A(2), A(3));
    if (!dst) {
        mesh_wasm_lane_fault(d, MESH_FAULT_VIEW_BOUNDS | A(1) << 8);
        return WASM_NUM_OK;
    }
    mesh_wasm_copy_out(d->mb, a, dst, A(3));
    return WASM_NUM_OK;
}

static int write_(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    uint64_t size;
    if (!wasm_memory(inst, &size)) return WASM_NUM_OK;
    size_t a;
    if (!mesh_wasm_lane_at(d, A(0), A(1), A(3), 1, &a)) return WASM_NUM_OK;
    const uint8_t *src = span(inst, A(2), A(3));
    if (!src) {
        mesh_wasm_lane_fault(d, MESH_FAULT_VIEW_BOUNDS | A(1) << 8);
        return WASM_NUM_OK;
    }
    mesh_wasm_copy_in(d->mb, a, src, A(3));
    return WASM_NUM_OK;
}

static int peer(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    io[0] = (uint32_t)mesh_wasm_lane_peer(d, A(0));
    return WASM_NUM_OK;
}

static int find(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    int32_t found = -1;
    uint64_t size;
    const uint8_t *name = wasm_memory(inst, &size) ? span(inst, A(0), A(1)) : NULL;
    uint32_t len = A(1);
    if (name && len <= MESH_NAME_MAX) {
        for (uint32_t s = 0; s < MESH_PROG_SLOTS && found < 0; s++) {
            size_t e = mesh_prog_entry(s);
            if (mesh_mailbox_get(d->mb, e + MESH_PT_STATE) == MESH_ST_EMPTY) continue;
            bool same = true;
            for (uint32_t i = 0; i < MESH_NAME_BYTES && same; i++)
                same = mesh_mailbox_get8(d->mb, e + MESH_PT_NAME + i) == (i < len ? name[i] : 0);
            if (same) found = (int32_t)s;
        }
    }
    io[0] = (uint32_t)found;
    return WASM_NUM_OK;
}

static int prog_state(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    io[0] = A(0) < MESH_PROG_SLOTS ? mesh_mailbox_get(d->mb, mesh_prog_entry(A(0)) + MESH_PT_STATE) : MESH_ST_EMPTY;
    return WASM_NUM_OK;
}

static int count(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    if (A(0) < MESH_COUNTERS) mesh_mailbox_fetch_add(d->mb, ctl(d, MESH_P_COUNTERS + 4 * (size_t)A(0)), A(1));
    return WASM_NUM_OK;
}

static int counter(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    io[0] = A(0) < MESH_COUNTERS ? mesh_mailbox_get(d->mb, ctl(d, MESH_P_COUNTERS + 4 * (size_t)A(0))) : 0;
    return WASM_NUM_OK;
}

static int input_pending(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    if (d->input_len == 0) {
        io[0] = 0;
        return WASM_NUM_OK;
    }
    size_t q = d->view_off + d->input_off;
    io[0] = (uint32_t)(mesh_mailbox_get(d->mb, q + MESH_IQ_HEAD) - mesh_mailbox_get(d->mb, q + MESH_IQ_TAIL));
    return WASM_NUM_OK;
}

static int input_pop(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    io[0] = (uint32_t)-1;
    if (d->input_len == 0) return WASM_NUM_OK;
    size_t q = d->view_off + d->input_off;
    uint32_t tail = mesh_mailbox_get(d->mb, q + MESH_IQ_TAIL);
    if (mesh_mailbox_get(d->mb, q + MESH_IQ_HEAD) == tail) return WASM_NUM_OK;
    uint8_t b = mesh_mailbox_get8(d->mb, q + MESH_IQ_DATA + (tail & (d->input_len - 1)));
    mesh_mailbox_publish(d->mb, q + MESH_IQ_TAIL, tail + 1u);
    io[0] = b;
    return WASM_NUM_OK;
}

static int respond(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    uint32_t len = A(0);
    if (len > d->resp_max) {
        mesh_wasm_lane_fault(d, MESH_FAULT_RESP_BOUNDS | len << 8);
        len = d->resp_max;
    }
    mesh_mailbox_set(d->mb, ctl(d, MESH_P_RESP_LEN), len);
    mesh_mailbox_publish64(d->mb, ctl(d, MESH_P_RESP_SEQ), mesh_mailbox_get64(d->mb, ctl(d, MESH_P_REQ_SEQ)));
    return WASM_NUM_OK;
}

static int exchange(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    if (!d->lockstep) {
        mesh_wasm_lane_fault(d, MESH_FAULT_PEER_VIEW | 0xffu << 8);
        io[0] = MESH_ROUND_TIMEOUT;
        return WASM_NUM_OK;
    }
    io[0] = mesh_round(d->mb, io[0], NULL);
    /* The wait is the cluster's, not the program's: the task's budget starts over. */
    if (d->epoch) wasm_set_deadline(inst, atomic_load(d->epoch) + d->ticks);
    return WASM_NUM_OK;
}

/* Pipes (mesh-abi pipe): records straight between linear memory and the ring. */
static int pipe_push(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    uint32_t to = A(0), ptr = A(1), len = A(2);
    uint64_t size;
    if (!wasm_memory(inst, &size)) {
        io[0] = (uint32_t)MESH_PIPE_SIZE;
        return WASM_NUM_OK;
    }
    const uint8_t *rec = span(inst, ptr, len);
    if (!rec) {
        mesh_wasm_lane_fault(d, MESH_FAULT_VIEW_BOUNDS | ptr << 8);
        io[0] = (uint32_t)MESH_PIPE_SIZE;
        return WASM_NUM_OK;
    }
    mesh_words w = mesh_mailbox_words(d->mb);
    int r = mesh_pipe_push(&w, d->slot, to, rec, len);
    if (r == -MESH_PIPE_ERR_NOT_CONNECTED || r == -MESH_PIPE_ERR_SIZE) mesh_wasm_lane_fault(d, MESH_FAULT_PIPE | to << 8);
    io[0] = (uint32_t)mesh_pipe_code(r);
    return WASM_NUM_OK;
}

static int pipe_pop(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    uint32_t ptr = A(0), len = A(1);
    uint64_t size;
    if (!wasm_memory(inst, &size)) {
        io[0] = (uint32_t)MESH_PIPE_SIZE;
        return WASM_NUM_OK;
    }
    uint8_t *out = span(inst, ptr, len);
    if (!out) {
        mesh_wasm_lane_fault(d, MESH_FAULT_VIEW_BOUNDS | ptr << 8);
        io[0] = (uint32_t)MESH_PIPE_SIZE;
        return WASM_NUM_OK;
    }
    mesh_words w = mesh_mailbox_words(d->mb);
    int r = mesh_pipe_pop(&w, d->slot, out, len);
    if (r == -MESH_PIPE_ERR_NOT_CONNECTED || r == -MESH_PIPE_ERR_SIZE) mesh_wasm_lane_fault(d, MESH_FAULT_PIPE | d->slot << 8);
    io[0] = (uint32_t)mesh_pipe_code(r);
    return WASM_NUM_OK;
}

static int pipe_len(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    mesh_words w = mesh_mailbox_words(d->mb);
    mesh_pipe_inbox in;
    int r = mesh_pipe_target(&w, d->slot, A(0), &in);
    io[0] = r == 0 ? (uint32_t)(int32_t)mesh_pipe_inbox_len(&in, &w) : (uint32_t)mesh_pipe_code(r);
    return WASM_NUM_OK;
}

static int host_ns(wasm_instance *inst, void *ctx, uint64_t *io) {
    (void)inst, (void)ctx;
    io[0] = mesh_host_ns();
    return WASM_NUM_OK;
}

static int fault(wasm_instance *inst, void *ctx, uint64_t *io) {
    UNUSED;
    D;
    mesh_wasm_lane_fault(d, A(0));
    return WASM_NUM_OK;
}

static const wasm_host_func IMPORTS[] = {
    {MESH_WASM_MODULE, "info", "i>i", info, NULL},
    {MESH_WASM_MODULE, "ld8", "ii>i", ld8, NULL},
    {MESH_WASM_MODULE, "ld32", "ii>i", ld32, NULL},
    {MESH_WASM_MODULE, "ld64", "ii>l", ld64, NULL},
    {MESH_WASM_MODULE, "st8", "iii>", st8, NULL},
    {MESH_WASM_MODULE, "st32", "iii>", st32, NULL},
    {MESH_WASM_MODULE, "st64", "iil>", st64, NULL},
    {MESH_WASM_MODULE, "cas32", "iiii>i", cas32, NULL},
    {MESH_WASM_MODULE, "add32", "iii>i", add32, NULL},
    {MESH_WASM_MODULE, "add64", "iil>l", add64, NULL},
    {MESH_WASM_MODULE, "read", "iiii>", read_, NULL},
    {MESH_WASM_MODULE, "write", "iiii>", write_, NULL},
    {MESH_WASM_MODULE, "peer", "i>i", peer, NULL},
    {MESH_WASM_MODULE, "find", "ii>i", find, NULL},
    {MESH_WASM_MODULE, "prog_state", "i>i", prog_state, NULL},
    {MESH_WASM_MODULE, "count", "ii>", count, NULL},
    {MESH_WASM_MODULE, "counter", "i>i", counter, NULL},
    {MESH_WASM_MODULE, "input_pending", ">i", input_pending, NULL},
    {MESH_WASM_MODULE, "input_pop", ">i", input_pop, NULL},
    {MESH_WASM_MODULE, "respond", "i>", respond, NULL},
    {MESH_WASM_MODULE, "exchange", "l>i", exchange, NULL},
    {MESH_WASM_MODULE, "pipe_push", "iii>i", pipe_push, NULL},
    {MESH_WASM_MODULE, "pipe_pop", "ii>i", pipe_pop, NULL},
    {MESH_WASM_MODULE, "pipe_len", "i>i", pipe_len, NULL},
    {MESH_WASM_MODULE, "host_ns", ">l", host_ns, NULL},
    {MESH_WASM_MODULE, "fault", "i>", fault, NULL},
};

const wasm_host_func *mesh_wasm_imports(size_t *n) {
    *n = sizeof IMPORTS / sizeof IMPORTS[0];
    return IMPORTS;
}
