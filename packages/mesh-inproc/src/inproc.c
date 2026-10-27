/* inproc.rs: Dev, Mb2, load, Prog, Heap, reads_mask. */
#include "mesh/inproc.h"
#include "mesh/loader.h"
#include "mesh/manifest_encode.h"
#include "mesh/mps.h"
#include "mesh/nvjitlink.h"
#include "mesh/words_host.h"

#include "mesh/abi/backend.h"
#include "mesh/abi/base.h"
#include "mesh/abi/control_block.h"
#include "mesh/abi/ext.h"
#include "mesh/abi/header.h"
#include "mesh/abi/input_queue.h"
#include "mesh/abi/node.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/ring.h"
#include "mesh/abi/trace.h"
#include "mesh/abi/view_heap.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t mesh_inproc_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

double mesh_ms_since(uint64_t t0) { return (double)(mesh_inproc_now_ns() - t0) / 1e6; }

bool mesh_dev_open_cuda(mesh_dev *out, char **err) {
    *out = (mesh_dev){0};
    if (!mesh_shm_new(&out->shm, err)) return false;
    mesh_nvjitlink jit;
    if (!mesh_nvjitlink_open(MESH_NVJITLINK_DIR, &jit, err)) return false;
    if (!mesh_cuda_backend_open(MESH_ISOLATION_MODE_BLINK, 0, NULL, 0, &out->shm, &jit, &out->b, err)) return false;
    out->comp = out->b->ops->compiler(out->b);
    out->kind = MESH_BACKEND_CUDA;
    out->ctx = mesh_cuda_backend_context(out->b);
    return true;
}

void mesh_dev_from(mesh_backend *b, const mesh_shm *shm, mesh_dev *out) {
    *out = (mesh_dev){b, b->ops->compiler(b), b->ops->caps(b)->kind, NULL, *shm};
}

void mesh_dev_close(mesh_dev *d) {
    mesh_backend_destroy(d->b);
    d->b = NULL;
}

const char *mesh_dev_code(const mesh_dev *d) {
    return d->kind == MESH_BACKEND_CUDA ? "cubin" : d->kind == MESH_BACKEND_METAL ? "MSL" : "wasm";
}

uint32_t mesh_dev_sms_total(const mesh_dev *d) { return d->b->ops->caps(d->b)->sms_total; }

static uint8_t *at(mesh_mb2 m, size_t off) {
    if (off >= MESH_MAILBOX_BYTES) {
        fprintf(stderr, "v2 offset 0x%zx\n", off);
        abort();
    }
    return m.base + off;
}
static _Atomic uint32_t *a32(mesh_mb2 m, size_t off) {
    if (off % 4) abort();
    return (_Atomic uint32_t *)(void *)at(m, off);
}
static _Atomic uint64_t *a64(mesh_mb2 m, size_t off) {
    if (off % 8) abort();
    return (_Atomic uint64_t *)(void *)at(m, off);
}

uint32_t mesh_mb2_r32(mesh_mb2 m, size_t off) { return atomic_load_explicit(a32(m, off), memory_order_acquire); }
void mesh_mb2_w32(mesh_mb2 m, size_t off, uint32_t v) { atomic_store_explicit(a32(m, off), v, memory_order_relaxed); }
void mesh_mb2_store32(mesh_mb2 m, size_t off, uint32_t v) { atomic_store_explicit(a32(m, off), v, memory_order_release); }
uint32_t mesh_mb2_add32(mesh_mb2 m, size_t off, uint32_t v) { return atomic_fetch_add_explicit(a32(m, off), v, memory_order_acq_rel); }
uint64_t mesh_mb2_r64(mesh_mb2 m, size_t off) { return atomic_load_explicit(a64(m, off), memory_order_acquire); }
void mesh_mb2_w64(mesh_mb2 m, size_t off, uint64_t v) { atomic_store_explicit(a64(m, off), v, memory_order_relaxed); }
void mesh_mb2_store64(mesh_mb2 m, size_t off, uint64_t v) { atomic_store_explicit(a64(m, off), v, memory_order_release); }
float mesh_mb2_rf(mesh_mb2 m, size_t off) {
    uint32_t u = mesh_mb2_r32(m, off);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
void mesh_mb2_wf(mesh_mb2 m, size_t off, float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    mesh_mb2_w32(m, off, u);
}
void mesh_mb2_bytes(mesh_mb2 m, size_t off, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) out[i] = *(volatile uint8_t *)at(m, off + i);
}
void mesh_mb2_write(mesh_mb2 m, size_t off, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) *(volatile uint8_t *)at(m, off + i) = b[i];
}
void mesh_mb2_zero(mesh_mb2 m, size_t off, size_t n) {
    for (size_t i = 0; i < n; i++) *(volatile uint8_t *)at(m, off + i) = 0;
}
mesh_words mesh_mb2_words(mesh_mb2 m) { return mesh_words_host(m.base); }

bool mesh_mb2_alloc(const mesh_dev *d, mesh_mb2 *out, char **err) {
    uint8_t *base;
    uint64_t dev;
    if (!d->b->ops->mailbox(d->b, &base, &dev, err)) return false;
    mesh_mb2 m = {base, dev};
    mesh_mb2_w32(m, MESH_H_MAGIC, MESH_MAGIC);
    mesh_mb2_w32(m, MESH_H_VERSION, MESH_VERSION);
    mesh_mb2_w32(m, MESH_H_PEER, MESH_PEER_NONE);
    mesh_mb2_w32(m, MESH_H_SYSTEMS, 1);
    mesh_mb2_w32(m, MESH_H_SMS, mesh_dev_sms_total(d));
    mesh_mb2_w32(m, MESH_H_BACKEND, mesh_caps_word(d->b->ops->caps(d->b)));
    *out = m;
    return true;
}

bool mesh_inproc_load(const mesh_dev *d, const char *dir, mesh_image2 *out, char **err) {
    *out = (mesh_image2){0};
    const char *slash = strrchr(dir, '/');
    char *root = slash ? mesh_strndup(dir, (size_t)(slash - dir)) : mesh_strdup(".");
    const char *name = slash ? slash + 1 : dir;
    if (!*root) {
        free(root);
        root = mesh_strdup("/");
    }
    mesh_payload payload;
    mesh_prepared p;
    if (!mesh_loader_read(root, name, d->comp->ops->needs_ptx(d->comp), &payload, err) ||
        !d->comp->ops->prepare(d->comp, &payload, NULL, root, &p, err)) {
        free(root);
        return false;
    }
    free(root);
    uint64_t t0 = mesh_inproc_now_ns();
    mesh_kernel *k = NULL;
    if (!d->b->ops->load(d->b, &p, &k, err)) {
        mesh_err_prefix(err, "load: ");
        mesh_prepared_free(&p);
        return false;
    }
    double load_ms = mesh_ms_since(t0);
    uint32_t rt_abi, regs;
    bool ok = mesh_kernel_global_u32(k, MESH_RT_ABI_SYMBOL, &rt_abi, err);
    if (ok && rt_abi != MESH_RT_ABI) ok = mesh_errf(err, "abi: %s = %u, meshd speaks %u", MESH_RT_ABI_SYMBOL, (unsigned)rt_abi, (unsigned)MESH_RT_ABI);
    if (ok) ok = mesh_kernel_regs(k, &regs, err);
    /* nodes = "fit" (as meshd's budget, alone on the device): 3/4 of what the occupancy allows. */
    if (ok && p.manifest.nodes_fit && d->b->ops->resident(d->b)) {
        uint32_t per_sm;
        ok = mesh_kernel_max_blocks_per_sm(k, &per_sm, err);
        if (ok) {
            uint32_t n = per_sm * d->b->ops->caps(d->b)->sms * 3 / 4;
            if (n > MESH_PROG_MAX_NODES) n = MESH_PROG_MAX_NODES;
            p.manifest.nodes = n ? n : 1;
        }
    }
    if (!ok) {
        d->b->ops->unload(d->b, k, NULL);
        mesh_prepared_free(&p);
        return false;
    }
    out->manifest = p.manifest;
    p.manifest = (mesh_manifest){0};
    out->kernel = k;
    out->hash = p.hash;
    out->cubin_bytes = (uint32_t)p.code_len;
    out->regs = regs;
    out->rt_abi = rt_abi;
    out->jit_ms = p.compile_ms;
    out->load_ms = load_ms;
    mesh_prepared_free(&p);
    return true;
}

void mesh_image2_free(const mesh_dev *d, mesh_image2 *im) {
    if (im->kernel) d->b->ops->unload(d->b, im->kernel, NULL);
    mesh_manifest_free(&im->manifest);
    *im = (mesh_image2){0};
}

bool mesh_prog_install(const mesh_dev *d, mesh_mb2 mb, uint32_t slot, mesh_image2 *image, uint32_t view_off, uint32_t reads_mask,
                       mesh_prog *out, char **err) {
    const mesh_manifest *m = &image->manifest;
    if ((m->view_bytes > 0 && view_off < MESH_VIEW_HEAP) || (size_t)view_off + m->view_bytes > MESH_VIEW_HEAP_END)
        return mesh_errf(err, "view 0x%x+0x%x outside the view heap", (unsigned)view_off, (unsigned)m->view_bytes);
    size_t e = mesh_prog_entry(slot);
    uint32_t gen = mesh_mb2_r32(mb, e + MESH_PT_GEN);
    mesh_mb2_store32(mb, e + MESH_PT_GEN, gen | 1);
    atomic_thread_fence(memory_order_seq_cst);
    mesh_image im = {view_off, reads_mask, image->hash, image->regs, image->cubin_bytes, image->rt_abi, 1};
    uint8_t bytes[MESH_PT_BYTES], ext[MESH_PX_BYTES];
    mesh_manifest_encode_entry(m, &im, bytes);
    mesh_mb2_write(mb, e + 8, bytes + 8, MESH_PT_BYTES - 8); /* PT_STATE and PT_GEN stay ours */
    mesh_manifest_encode_ext(m, ext);
    uint64_t scratch = mesh_kernel_scratch(image->kernel);
    for (int i = 0; i < 8; i++) ext[MESH_PX_SCRATCH + (size_t)i] = (uint8_t)(scratch >> (8 * i));
    mesh_mb2_write(mb, mesh_prog_ext(slot), ext, MESH_PX_BYTES);
    mesh_mb2_zero(mb, mesh_prog_ctl(slot), MESH_PROG_CTL_BYTES);
    mesh_mb2_zero(mb, mesh_node_rec(slot, 0), MESH_PROG_NODES_BYTES);
    mesh_mb2_zero(mb, mesh_trace_rec(slot, 0), (size_t)MESH_TRACE_BYTES * MESH_TRACE_PER_PROG);
    mesh_mb2_zero(mb, view_off, m->view_bytes);
    mesh_mb2_w32(mb, e + MESH_PT_STATE, MESH_ST_LOADED);
    mesh_mb2_store32(mb, e + MESH_PT_GEN, (gen | 1) + 1);
    mesh_mb2_add32(mb, MESH_H_PROG_SEQ, 1);
    mesh_stream *st = NULL;
    if (!d->b->ops->new_stream(d->b, slot, image->kernel, &st, err)) return false;
    *out = (mesh_prog){mb, slot, st, *image, m->nodes, 0};
    *image = (mesh_image2){0};
    return true;
}

void mesh_prog_free(const mesh_dev *d, mesh_prog *p) {
    mesh_stream_drop(p->stream);
    mesh_image2_free(d, &p->image);
}

size_t mesh_prog_ctl_off(const mesh_prog *p, size_t off) { return mesh_prog_ctl(p->slot) + off; }
size_t mesh_prog_view(const mesh_prog *p) { return mesh_mb2_r32(p->mb, mesh_prog_entry(p->slot) + MESH_PT_VIEW_OFF); }
uint32_t mesh_prog_head(const mesh_prog *p) { return mesh_mb2_r32(p->mb, mesh_prog_ctl_off(p, MESH_P_RING_HEAD)); }
uint32_t mesh_prog_counter(const mesh_prog *p, uint32_t i) { return mesh_mb2_r32(p->mb, mesh_prog_ctl_off(p, MESH_P_COUNTERS + 4 * i)); }

bool mesh_prog_launch(const mesh_dev *d, mesh_prog *p, double *ms, char **err) {
    mesh_mb2 mb = p->mb;
    bool done = false;
    if (!d->b->ops->query(d->b, p->stream, p->image.kernel, &done, err)) return false;
    if (!done) return mesh_errf(err, "slot %u: still running", (unsigned)p->slot);
    mesh_mb2_w32(mb, mesh_prog_ctl_off(p, MESH_P_RING_HEAD), p->tail);
    mesh_mb2_w32(mb, mesh_prog_ctl_off(p, MESH_P_ONLINE), 0);
    mesh_mb2_w32(mb, mesh_prog_ctl_off(p, MESH_P_HALTED), 0);
    mesh_mb2_store32(mb, mesh_prog_ctl_off(p, MESH_P_SHUTDOWN), 0);
    mesh_mb2_add32(mb, mesh_prog_ctl_off(p, MESH_P_LAUNCHES), 1);
    atomic_thread_fence(memory_order_seq_cst);
    uint64_t t0 = mesh_inproc_now_ns();
    if (!d->b->ops->launch(d->b, p->stream, p->image.kernel, p->nodes, mb.dev, p->slot, err)) return false;
    while (mesh_mb2_r32(mb, mesh_prog_ctl_off(p, MESH_P_ONLINE)) < p->nodes) {
        if (mesh_ms_since(t0) > 2000) {
            bool fin = false;
            char *e2 = NULL;
            const char *what = !d->b->ops->query(d->b, p->stream, p->image.kernel, &fin, &e2) ? e2 : fin ? "exited" : "running";
            mesh_errf(err, "slot %u: %u of %u nodes online after 2 s (stream: %s)", (unsigned)p->slot,
                      (unsigned)mesh_mb2_r32(mb, mesh_prog_ctl_off(p, MESH_P_ONLINE)), (unsigned)p->nodes, what);
            free(e2);
            return false;
        }
    }
    *ms = mesh_ms_since(t0);
    mesh_mb2_w32(mb, mesh_prog_entry(p->slot) + MESH_PT_STATE, MESH_ST_RUNNING);
    return true;
}

bool mesh_prog_stop(const mesh_dev *d, mesh_prog *p, double *ms, char **err) {
    mesh_mb2 mb = p->mb;
    uint64_t t0 = mesh_inproc_now_ns();
    mesh_mb2_store32(mb, mesh_prog_ctl_off(p, MESH_P_SHUTDOWN), 1);
    for (;;) {
        bool done = false;
        if (mesh_mb2_r32(mb, mesh_prog_ctl_off(p, MESH_P_HALTED)) >= p->nodes) {
            if (!d->b->ops->query(d->b, p->stream, p->image.kernel, &done, err)) return false;
            if (done) break;
        }
        if (mesh_ms_since(t0) > 3000)
            return mesh_errf(err, "slot %u: %u of %u nodes halted in 3 s", (unsigned)p->slot,
                             (unsigned)mesh_mb2_r32(mb, mesh_prog_ctl_off(p, MESH_P_HALTED)), (unsigned)p->nodes);
    }
    *ms = mesh_ms_since(t0);
    mesh_mb2_w32(mb, mesh_prog_entry(p->slot) + MESH_PT_STATE, MESH_ST_LOADED);
    return true;
}

bool mesh_prog_done(const mesh_prog *p, uint32_t t) {
    return mesh_mb2_r32(p->mb, mesh_prog_ctl_off(p, MESH_P_MARKS + 4 * (t % MESH_PROG_RESULTS))) == t + 1;
}

bool mesh_prog_wait(const mesh_prog *p, uint32_t t, uint32_t *result, char **err) {
    uint64_t t0 = mesh_inproc_now_ns();
    while (!mesh_prog_done(p, t)) {
        if (mesh_ms_since(t0) > 5000)
            return mesh_errf(err, "slot %u ticket %u not completed in 5 s (head %u, done %u, online %u, fault 0x%x)", (unsigned)p->slot,
                             (unsigned)t, (unsigned)mesh_prog_head(p), (unsigned)mesh_mb2_r32(p->mb, mesh_prog_ctl_off(p, MESH_P_DONE)),
                             (unsigned)mesh_mb2_r32(p->mb, mesh_prog_ctl_off(p, MESH_P_ONLINE)),
                             (unsigned)mesh_mb2_r32(p->mb, mesh_prog_ctl_off(p, MESH_P_FAULT)));
    }
    *result = mesh_mb2_r32(p->mb, mesh_prog_ctl_off(p, MESH_P_RESULTS + 4 * (t % MESH_PROG_RESULTS)));
    return true;
}

bool mesh_prog_publish(mesh_prog *p, uint32_t op, uint32_t arg, uint32_t *ticket, char **err) {
    uint32_t t = p->tail, r;
    if (t >= MESH_PROG_RING && !mesh_prog_wait(p, t - MESH_PROG_RING, &r, err)) return false;
    size_t e = mesh_prog_ctl_off(p, MESH_P_RING + MESH_ENTRY_BYTES * (t % MESH_PROG_RING));
    mesh_mb2_w32(p->mb, e + MESH_E_OP, op);
    mesh_mb2_w32(p->mb, e + MESH_E_ARG, arg);
    mesh_mb2_w32(p->mb, e + MESH_E_SLOT, t % MESH_PROG_RESULTS);
    mesh_mb2_store32(p->mb, e + MESH_E_SEQ, t + 1);
    p->tail = t + 1;
    mesh_mb2_w64(p->mb, mesh_prog_ctl_off(p, MESH_P_RING_TAIL), p->tail);
    *ticket = t;
    return true;
}

bool mesh_prog_run(mesh_prog *p, uint32_t op, uint32_t arg, uint32_t *result, char **err) {
    uint32_t t;
    return mesh_prog_publish(p, op, arg, &t, err) && mesh_prog_wait(p, t, result, err);
}

bool mesh_prog_batch(mesh_prog *p, uint32_t op, uint32_t n, uint32_t *results, char **err) {
    uint32_t first = p->tail;
    for (uint32_t a = 0; a < n; a++) {
        uint32_t t;
        if (!mesh_prog_publish(p, op, a, &t, err)) return false;
    }
    for (uint32_t a = 0; a < n; a++)
        if (!mesh_prog_wait(p, first + a, &results[a], err)) return false;
    return true;
}

bool mesh_prog_input_push(const mesh_prog *p, const uint8_t *b, size_t n, char **err) {
    const mesh_manifest *m = &p->image.manifest;
    if (m->input_len == 0) return mesh_errf(err, "slot %u: no input queue", (unsigned)p->slot);
    size_t q = mesh_prog_view(p) + m->input_off;
    uint32_t head = mesh_mb2_r32(p->mb, q + MESH_IQ_HEAD), tail = mesh_mb2_r32(p->mb, q + MESH_IQ_TAIL);
    if ((size_t)(head - tail) + n > m->input_len)
        return mesh_errf(err, "slot %u: input queue full (%u of %u)", (unsigned)p->slot, (unsigned)(head - tail), (unsigned)m->input_len);
    for (size_t i = 0; i < n; i++) mesh_mb2_write(p->mb, q + MESH_IQ_DATA + (head + i) % m->input_len, &b[i], 1);
    mesh_mb2_store32(p->mb, q + MESH_IQ_HEAD, head + (uint32_t)n);
    return true;
}

bool mesh_prog_request(mesh_prog *p, const uint8_t *req, size_t n, uint8_t **resp, size_t *rn, char **err) {
    if (!p->image.manifest.has_request) return mesh_errf(err, "slot %u: no request window", (unsigned)p->slot);
    mesh_manifest_window w = p->image.manifest.request;
    size_t v = mesh_prog_view(p);
    if (n > w.req_max) n = w.req_max;
    uint8_t *pad = mesh_alloc(w.req_max ? w.req_max : 1);
    memcpy(pad, req, n);
    mesh_mb2_write(p->mb, v + w.req_off, pad, w.req_max);
    free(pad);
    mesh_mb2_w32(p->mb, mesh_prog_ctl_off(p, MESH_P_REQ_LEN), (uint32_t)n);
    uint64_t seq = mesh_mb2_r64(p->mb, mesh_prog_ctl_off(p, MESH_P_REQ_SEQ)) + 1;
    mesh_mb2_store64(p->mb, mesh_prog_ctl_off(p, MESH_P_REQ_SEQ), seq);
    uint32_t r;
    if (!mesh_prog_run(p, MESH_OP_REQ, 0, &r, err)) return false;
    uint64_t rs = mesh_mb2_r64(p->mb, mesh_prog_ctl_off(p, MESH_P_RESP_SEQ));
    if (rs != seq) return mesh_errf(err, "slot %u: P_RESP_SEQ %llu != P_REQ_SEQ %llu", (unsigned)p->slot, (unsigned long long)rs, (unsigned long long)seq);
    size_t len = mesh_mb2_r32(p->mb, mesh_prog_ctl_off(p, MESH_P_RESP_LEN));
    if (len > w.resp_max) len = w.resp_max;
    *resp = mesh_alloc(len ? len : 1);
    mesh_mb2_bytes(p->mb, v + w.resp_off, *resp, len);
    *rn = len;
    return true;
}

mesh_heap mesh_heap_new(void) { return (mesh_heap){(uint32_t)MESH_VIEW_HEAP}; }

bool mesh_heap_alloc(mesh_heap *h, uint32_t bytes, uint32_t *at_out, char **err) {
    uint32_t a = h->next;
    size_t end = a + ((size_t)bytes + MESH_VIEW_PAGE - 1) / MESH_VIEW_PAGE * MESH_VIEW_PAGE;
    if (end > MESH_VIEW_HEAP_END) return mesh_errf(err, "view heap: 0x%x bytes do not fit", (unsigned)bytes);
    h->next = (uint32_t)end;
    *at_out = a;
    return true;
}

bool mesh_reads_mask(const mesh_manifest *m, const mesh_slot_name *slots, size_t n, uint32_t *mask, char **err) {
    uint32_t r = 0;
    for (size_t i = 0; i < m->nreads; i++) {
        if (strcmp(m->reads[i], "*") == 0) {
            *mask = MESH_READS_ALL;
            return true;
        }
        bool found = false;
        for (size_t j = 0; j < n && !found; j++)
            if (strcmp(slots[j].name, m->reads[i]) == 0) {
                r |= 1u << slots[j].slot;
                found = true;
            }
        if (!found) {
            char *q = mesh_debug_str(m->reads[i]);
            mesh_errf(err, "%s: reads %s, which is not loaded", m->name, q);
            free(q);
            return false;
        }
    }
    *mask = r;
    return true;
}
