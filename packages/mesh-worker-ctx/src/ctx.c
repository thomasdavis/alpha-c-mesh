/* mesh-worker lib.rs's View and Ctx. */
#include "mesh/worker_ctx.h"

#include "mesh/abi/base.h"
#include "mesh/abi/control_block.h"
#include "mesh/abi/ext.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/header.h"
#include "mesh/abi/input_queue.h"
#include "mesh/abi/pipe.h"
#include "mesh/abi/program_table.h"

#include <sched.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

uint64_t mesh_worker_now_ns(void) {
    struct timespec t = {0, 0};
#if defined(__APPLE__)
    clock_gettime(CLOCK_UPTIME_RAW, &t);
#else
    clock_gettime(CLOCK_MONOTONIC, &t);
#endif
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static void fault(uint8_t *ctl, uint32_t code) {
    _Atomic uint32_t *w = (_Atomic uint32_t *)(void *)(ctl + MESH_P_FAULT);
    uint32_t zero = 0;
    atomic_compare_exchange_strong_explicit(w, &zero, code, memory_order_acq_rel, memory_order_relaxed);
}

static bool ok(mesh_view v, uint32_t off, uint32_t n, uint32_t align) {
    uint64_t end = (uint64_t)off + n;
    bool fine = end <= v.len && off % align == 0 && ((uintptr_t)v.base + off) % align == 0;
    if (!fine) fault(v.ctl, MESH_FAULT_VIEW_BOUNDS | off << 8);
    return fine;
}

static _Atomic uint32_t *w32(mesh_view v, uint32_t off) { return (_Atomic uint32_t *)(void *)(v.base + off); }
static _Atomic uint64_t *w64(mesh_view v, uint32_t off) { return (_Atomic uint64_t *)(void *)(v.base + off); }

uint32_t mesh_view_len(mesh_view v) { return v.len; }
uint32_t mesh_view_u32(mesh_view v, uint32_t off) { return ok(v, off, 4, 4) ? atomic_load_explicit(w32(v, off), memory_order_acquire) : 0; }
float mesh_view_f32(mesh_view v, uint32_t off) {
    uint32_t u = mesh_view_u32(v, off);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
uint64_t mesh_view_u64(mesh_view v, uint32_t off) { return ok(v, off, 8, 8) ? atomic_load_explicit(w64(v, off), memory_order_acquire) : 0; }
uint8_t mesh_view_u8(mesh_view v, uint32_t off) {
    return ok(v, off, 1, 1) ? atomic_load_explicit((_Atomic uint8_t *)(void *)(v.base + off), memory_order_relaxed) : 0;
}
void mesh_view_set_u32(mesh_view v, uint32_t off, uint32_t x) {
    if (ok(v, off, 4, 4)) atomic_store_explicit(w32(v, off), x, memory_order_release);
}
void mesh_view_set_f32(mesh_view v, uint32_t off, float x) {
    uint32_t u;
    memcpy(&u, &x, 4);
    mesh_view_set_u32(v, off, u);
}
void mesh_view_set_u64(mesh_view v, uint32_t off, uint64_t x) {
    if (ok(v, off, 8, 8)) atomic_store_explicit(w64(v, off), x, memory_order_release);
}
void mesh_view_set_u8(mesh_view v, uint32_t off, uint8_t x) {
    if (ok(v, off, 1, 1)) atomic_store_explicit((_Atomic uint8_t *)(void *)(v.base + off), x, memory_order_relaxed);
}
uint32_t mesh_view_add_u32(mesh_view v, uint32_t off, uint32_t x) {
    return ok(v, off, 4, 4) ? atomic_fetch_add_explicit(w32(v, off), x, memory_order_acq_rel) : 0;
}
bool mesh_view_read(mesh_view v, uint32_t off, uint8_t *out, size_t n) {
    if (n > UINT32_MAX || !ok(v, off, (uint32_t)n, 1)) {
        memset(out, 0, n);
        return false;
    }
    memcpy(out, v.base + off, n);
    return true;
}
bool mesh_view_write(mesh_view v, uint32_t off, const uint8_t *b, size_t n) {
    if (n > UINT32_MAX || !ok(v, off, (uint32_t)n, 1)) return false;
    memcpy(v.base + off, b, n);
    return true;
}
bool mesh_view_write_f32s(mesh_view v, uint32_t off, const float *x, size_t n) { return mesh_view_write(v, off, (const uint8_t *)x, n * 4); }
bool mesh_view_read_f32s(mesh_view v, uint32_t off, float *out, size_t n) { return mesh_view_read(v, off, (uint8_t *)out, n * 4); }
bool mesh_view_sub(mesh_view v, uint32_t off, uint32_t len, mesh_view *out) {
    if (!ok(v, off, len, 1)) return false;
    *out = (mesh_view){v.base + off, len, v.ctl};
    return true;
}

void mesh_worker_entry(mesh_mailbox mb, uint32_t s, uint32_t out[32]) {
    size_t e = mesh_prog_entry(s);
    for (;;) {
        uint32_t g = mesh_mailbox_get(mb, e + MESH_PT_GEN);
        if ((g & 1) == 0) {
            for (size_t i = 0; i < MESH_PT_BYTES / 4; i++)
                out[i] = atomic_load_explicit((_Atomic uint32_t *)(void *)(mb.base + e + 4 * i), memory_order_relaxed);
            atomic_thread_fence(memory_order_acquire);
            if (mesh_mailbox_get(mb, e + MESH_PT_GEN) == g) return;
        }
        sched_yield();
    }
}

void mesh_worker_ctx_new(mesh_mailbox mb, uint32_t slot, uint32_t node, mesh_worker_ctx *c) {
    uint32_t e[32];
    mesh_worker_entry(mb, slot, e);
#define F(o) e[(o) / 4]
    size_t ctl = mesh_prog_ctl(slot);
    *c = (mesh_worker_ctx){
        .mb = mb, .ctl = ctl, .slot = slot, .node = node,
        .view = {mb.base + F(MESH_PT_VIEW_OFF), F(MESH_PT_VIEW_BYTES), mb.base + ctl},
        .nodes = F(MESH_PT_NODES), .peer_mode = F(MESH_PT_PEER), .input_off = F(MESH_PT_INPUT_OFF), .input_len = F(MESH_PT_INPUT_LEN),
        .req_off = F(MESH_PT_REQ_OFF), .req_max = F(MESH_PT_REQ_MAX), .resp_off = F(MESH_PT_RESP_OFF), .resp_max = F(MESH_PT_RESP_MAX),
    };
#undef F
}

static uint32_t relaxed(mesh_mailbox mb, size_t off) {
    return atomic_load_explicit((_Atomic uint32_t *)(void *)(mb.base + off), memory_order_relaxed);
}
static uint8_t *ctlp(const mesh_worker_ctx *c) { return c->mb.base + c->ctl; }

uint32_t mesh_ctx_system(const mesh_worker_ctx *c) { return relaxed(c->mb, MESH_H_PEER); }
uint32_t mesh_ctx_systems(const mesh_worker_ctx *c) { return relaxed(c->mb, MESH_H_SYSTEMS); }
bool mesh_ctx_group(const mesh_worker_ctx *c, uint32_t *rank, uint32_t *size) {
    uint32_t r = relaxed(c->mb, MESH_H_GROUP_RANK), n = relaxed(c->mb, MESH_H_GROUP_N);
    if (n < 2 || n > MESH_MAX_SYSTEMS || r >= n) return false;
    *rank = r;
    *size = n;
    return true;
}
uint32_t mesh_ctx_param(const mesh_worker_ctx *c) { return relaxed(c->mb, mesh_prog_ext(c->slot) + MESH_PX_PARAM); }
uint32_t mesh_ctx_sms(const mesh_worker_ctx *c) { return relaxed(c->mb, MESH_H_SMS); }

bool mesh_ctx_peer_view(const mesh_worker_ctx *c, uint32_t s, mesh_view *out) {
    if (s >= MESH_PROG_SLOTS) return false;
    uint32_t mine[32], e[32];
    mesh_worker_entry(c->mb, c->slot, mine);
    if ((mine[MESH_PT_READS_MASK / 4] & (1u << s)) == 0) {
        fault(ctlp(c), MESH_FAULT_PEER_VIEW | s << 8);
        return false;
    }
    mesh_worker_entry(c->mb, s, e);
    uint32_t st = e[MESH_PT_STATE / 4], off = e[MESH_PT_VIEW_OFF / 4], len = e[MESH_PT_VIEW_BYTES / 4];
    if (st == MESH_ST_EMPTY || st == MESH_ST_FAILED || (size_t)off + len > MESH_MAILBOX_BYTES) return false;
    *out = (mesh_view){c->mb.base + off, len, ctlp(c)};
    return true;
}

uint32_t mesh_ctx_prog_state(const mesh_worker_ctx *c, uint32_t s) { return relaxed(c->mb, mesh_prog_entry(s) + MESH_PT_STATE); }

void mesh_ctx_prog_name(const mesh_worker_ctx *c, uint32_t s, char out[17]) {
    uint32_t e[32];
    mesh_worker_entry(c->mb, s, e);
    memcpy(out, (const uint8_t *)e + MESH_PT_NAME, MESH_NAME_BYTES);
    out[MESH_NAME_BYTES] = 0;
}

bool mesh_ctx_find(const mesh_worker_ctx *c, const char *name, uint32_t *slot) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (mesh_ctx_prog_state(c, s) == MESH_ST_EMPTY) continue;
        char n[17];
        mesh_ctx_prog_name(c, s, n);
        if (strcmp(n, name) == 0) {
            *slot = s;
            return true;
        }
    }
    return false;
}

bool mesh_ctx_mailbox(const mesh_worker_ctx *c, mesh_mailbox *out) {
    uint32_t e[32];
    mesh_worker_entry(c->mb, c->slot, e);
    if (e[MESH_PT_READS_MASK / 4] != MESH_READS_ALL) return false;
    *out = c->mb;
    return true;
}

static size_t queue_at(const mesh_worker_ctx *c) { return (size_t)(c->view.base - c->mb.base) + c->input_off; }

uint32_t mesh_ctx_input_pending(const mesh_worker_ctx *c) {
    if (c->input_len == 0) return 0;
    size_t q = queue_at(c);
    return mesh_mailbox_get(c->mb, q + MESH_IQ_HEAD) - mesh_mailbox_get(c->mb, q + MESH_IQ_TAIL);
}

bool mesh_ctx_input_pop(const mesh_worker_ctx *c, uint8_t *out) {
    if (c->input_len == 0) return false;
    size_t q = queue_at(c);
    for (;;) {
        uint32_t tail = mesh_mailbox_get(c->mb, q + MESH_IQ_TAIL);
        if (mesh_mailbox_get(c->mb, q + MESH_IQ_HEAD) == tail) return false;
        size_t at = q + MESH_IQ_DATA + (tail & (c->input_len - 1));
        uint8_t b = (uint8_t)(mesh_mailbox_get(c->mb, at & ~(size_t)3) >> (8 * (at & 3)));
        if (mesh_mailbox_cas(c->mb, q + MESH_IQ_TAIL, tail, tail + 1) == tail) {
            *out = b;
            return true;
        }
    }
}

int32_t mesh_ctx_push_code(const mesh_worker_ctx *c, uint32_t s, const uint8_t *rec, size_t n) {
    mesh_words w = mesh_mailbox_words(c->mb);
    int r = mesh_pipe_push(&w, c->slot, s, rec, n);
    if (r == -MESH_PIPE_ERR_NOT_CONNECTED || r == -MESH_PIPE_ERR_SIZE) fault(ctlp(c), MESH_FAULT_PIPE | s << 8);
    return mesh_pipe_code(r);
}

bool mesh_ctx_push(const mesh_worker_ctx *c, uint32_t s, const uint8_t *rec, size_t n) { return mesh_ctx_push_code(c, s, rec, n) == MESH_PIPE_OK; }

bool mesh_ctx_pop(const mesh_worker_ctx *c, uint8_t *out, size_t n) {
    mesh_words w = mesh_mailbox_words(c->mb);
    int r = mesh_pipe_pop(&w, c->slot, out, n);
    if (r == -MESH_PIPE_ERR_NOT_CONNECTED || r == -MESH_PIPE_ERR_SIZE) fault(ctlp(c), MESH_FAULT_PIPE | c->slot << 8);
    return r == 1;
}

bool mesh_ctx_pipe_len(const mesh_worker_ctx *c, uint32_t s, uint32_t *len) {
    mesh_words w = mesh_mailbox_words(c->mb);
    mesh_pipe_inbox in;
    if (mesh_pipe_target(&w, c->slot, s, &in) < 0) return false;
    *len = mesh_pipe_inbox_len(&in, &w);
    return true;
}

mesh_view mesh_ctx_request(const mesh_worker_ctx *c) {
    uint32_t len = relaxed(c->mb, c->ctl + MESH_P_REQ_LEN);
    if (len > c->req_max) len = c->req_max;
    return (mesh_view){c->view.base + c->req_off, len, ctlp(c)};
}

mesh_view mesh_ctx_response(const mesh_worker_ctx *c) { return (mesh_view){c->view.base + c->resp_off, c->resp_max, ctlp(c)}; }

void mesh_ctx_respond(const mesh_worker_ctx *c, uint32_t len) {
    if (len > c->resp_max) {
        fault(ctlp(c), MESH_FAULT_RESP_BOUNDS | len << 8);
        len = c->resp_max;
    }
    mesh_mailbox_set(c->mb, c->ctl + MESH_P_RESP_LEN, len);
    mesh_mailbox_publish64(c->mb, c->ctl + MESH_P_RESP_SEQ, mesh_mailbox_get64(c->mb, c->ctl + MESH_P_REQ_SEQ));
}

void mesh_ctx_count(const mesh_worker_ctx *c, uint32_t i, uint32_t n) {
    if (i < MESH_COUNTERS) mesh_mailbox_fetch_add(c->mb, c->ctl + MESH_P_COUNTERS + 4 * i, n);
}

uint32_t mesh_ctx_counter(const mesh_worker_ctx *c, uint32_t i) {
    return i < MESH_COUNTERS ? relaxed(c->mb, c->ctl + MESH_P_COUNTERS + 4 * i) : 0;
}

bool mesh_ctx_request_run(const mesh_worker_ctx *c, uint32_t s, bool on) {
    mesh_mailbox all;
    if (!mesh_ctx_mailbox(c, &all) || s >= MESH_PROG_SLOTS) return false;
    size_t w = mesh_prog_ctl(s) + MESH_P_RUN_REQ;
    uint32_t cur = mesh_mailbox_get(c->mb, w);
    for (;;) {
        uint32_t seen = mesh_mailbox_cas(c->mb, w, cur, mesh_run_req_next(cur, on));
        if (seen == cur) return true;
        cur = seen;
    }
}
