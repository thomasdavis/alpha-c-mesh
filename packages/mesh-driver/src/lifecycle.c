/* Per tick and per slot: run requests, completions, phases, publishing; launching, stopping, failing,
 * unloading; pipes between programs; the http program's load requests. */
#include "driver_int.h"

#include "mesh/cpuuse.h"
#include "mesh/rsfmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void drv_native_words(mesh_driver *d, uint32_t s);

#define PUSH(arr, n, cap, v)                                                          \
    do {                                                                              \
        if ((n) == (cap)) {                                                           \
            (cap) = (cap) ? 2 * (cap) : 8;                                            \
            (arr) = mesh_xrealloc((arr), (cap) * sizeof *(arr));                      \
        }                                                                             \
        (arr)[(n)++] = (v);                                                           \
    } while (0)

static size_t ctl(uint32_t s, size_t off) { return mesh_prog_ctl(s) + off; }

/* A stream reported an error: the CUDA context is sticky-broken. */
void drv_context_fault(mesh_driver *d, const char *e) {
    if (d->fault) return;
    mesh_error("kernel fault: %s; the CUDA context is lost, restart meshd", e);
    d->fault = mesh_xstrdup(e);
    /* The CPU executors (wasm lanes, native workers) do not run in the context: they go on. */
    uint32_t gpu = drv_gpu_kind(d);
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (x && x->image.exec == gpu && !x->failed) x->failed = mesh_buf_fmt("CUDA context lost: %s", e);
    }
}

/* Appends to the slot's input queue (IQ_*): bytes at IQ_DATA + head % len, then head (release). */
int drv_input(mesh_driver *d, uint32_t s, const uint8_t *bytes, size_t n, char **text) {
    mesh_mailbox mb = d->sh->mb;
    dslot *x = d->slots[s];
    const mesh_manifest *m = &x->image.manifest;
    if (m->input_len == 0) { *text = mesh_buf_fmt("%s declares no input queue", m->name); return -1; }
    size_t q = (size_t)x->view_off + m->input_off;
    uint32_t head = mesh_mailbox_get(mb, q + MESH_IQ_HEAD), tail = mesh_mailbox_get(mb, q + MESH_IQ_TAIL);
    uint32_t used = head - tail < m->input_len ? head - tail : m->input_len;
    uint32_t free_ = m->input_len - used;
    if (n > free_) { *text = mesh_buf_fmt("%s: input queue full (%u bytes free)", m->name, free_); return -1; }
    uint32_t h = head;
    for (size_t i = 0; i < n; i++) {
        /* A word RMW: the GPU only reads the data bytes, and only those before head. */
        size_t at = q + MESH_IQ_DATA + (h % m->input_len);
        size_t w = at & ~(size_t)3, sh = 8 * (at & 3);
        mesh_mailbox_set(mb, w, (mesh_mailbox_get(mb, w) & ~(0xffu << sh)) | (uint32_t)bytes[i] << sh);
        h++;
    }
    mesh_mailbox_publish(mb, q + MESH_IQ_HEAD, h);
    *text = mesh_buf_fmt("%zu bytes", n);
    return 0;
}

/* ---------------------------------------------------------------- pipes */

/* Releases abandoned pipe cells whose consumers reached them. */
static void dead_cells_tick(mesh_driver *d) {
    if (!d->ndead) return;
    mesh_mailbox mb = d->sh->mb;
    mesh_words w = mesh_mailbox_words(mb);
    size_t k = 0;
    for (size_t i = 0; i < d->ndead; i++) {
        ddead *c = &d->dead[i];
        mesh_pipe_inbox now;
        /* The consumer's inbox as it is now: another view (upgraded, unloaded) is not that ring any more. */
        if (mesh_pipe_target(&w, c->consumer, c->consumer, &now) != 0 || now.base != c->inbox.base || now.cap != c->inbox.cap ||
            now.rec != c->inbox.rec)
            continue;
        mesh_pipe_ring r = mesh_pipe_inbox_side(&c->inbox, c->side);
        int got = mesh_pipe_ring_skip_dead(&r, &w, c->producer, c->pos);
        if (got == 1) {
            char cn[32], pn[32];
            if (d->slots[c->consumer]) snprintf(cn, sizeof cn, "%s", drv_name(d->slots[c->consumer]));
            else snprintf(cn, sizeof cn, "slot %u", c->consumer);
            if (d->slots[c->producer]) snprintf(pn, sizeof pn, "%s", drv_name(d->slots[c->producer]));
            else snprintf(pn, sizeof pn, "slot %u", c->producer);
            mesh_info("pipes: %s's inbox: released position %u that %s reserved and never committed", cn, c->pos, pn);
            continue;
        }
        if (got == 0) d->dead[k++] = *c;
    }
    d->ndead = k;
}

typedef struct {
    mesh_driver *d;
    uint32_t consumer, side, producer;
    mesh_pipe_inbox inbox;
    size_t found;
} abandon_ctx;

static void on_abandoned(void *arg, uint32_t pos) {
    abandon_ctx *a = arg;
    mesh_driver *d = a->d;
    for (size_t i = 0; i < d->ndead; i++)
        if (d->dead[i].consumer == a->consumer && d->dead[i].side == a->side && d->dead[i].pos == pos) return;
    ddead c = {a->consumer, a->inbox, a->side, a->producer, pos};
    PUSH(d->dead, d->ndead, d->cdead, c);
    a->found++;
}

/* Producer slot s runs no code any more: its pushes in flight are over, and the cells
 * it reserved and never committed are found for release. */
void drv_producer_stopped(mesh_driver *d, uint32_t s) {
    mesh_mailbox mb = d->sh->mb;
    mesh_words w = mesh_mailbox_words(mb);
    mesh_pt_flight_clear(mb, s);
    uint32_t feeds = d->slots[s] ? d->slots[s]->feeds_mask : 0;
    for (uint32_t c = 0; c < MESH_PROG_SLOTS; c++) {
        if (!((feeds >> c) & 1u)) continue;
        mesh_pipe_inbox inbox;
        if (mesh_pipe_target(&w, c, c, &inbox) != 0) continue;
        abandon_ctx a = {d, c, 0, s, inbox, 0};
        const uint32_t sides[2] = {MESH_PIPE_SIDE_CPU, MESH_PIPE_SIDE_GPU};
        for (int i = 0; i < 2; i++) {
            a.side = sides[i];
            mesh_pipe_ring r = mesh_pipe_inbox_side(&inbox, sides[i]);
            mesh_pipe_ring_abandoned(&r, &w, s, on_abandoned, &a);
        }
        if (a.found > 0)
            mesh_error("pipes: slot %u stopped with %zu cell(s) of slot %u's inbox reserved and not committed: released when the consumer reaches them",
                       s, a.found, c);
    }
}

/* Spins until no producer in fed has a push in flight that began before this
 * call, or limit passed; returns the producers not seen quiet (0: quiet). Two
 * rounds, as RCU's grace period. */
uint32_t drv_pipe_quiet(mesh_driver *d, uint32_t fed, int64_t limit_ns) {
    mesh_mailbox mb = d->sh->mb;
    if (!fed) return 0;
    int64_t t0 = mesh_mono_ns();
    for (int round = 0; round < 2; round++) {
        uint32_t parity = mesh_pt_flight_epoch(mb), left = fed;
        for (;;) {
            for (uint32_t x = 0; x < MESH_PROG_SLOTS; x++)
                if (((left >> x) & 1u) && mesh_pt_flight(mb, x, parity) == 0) left &= ~(1u << x);
            if (!left) break;
            if (mesh_mono_ns() - t0 > limit_ns) return left;
        }
    }
    return 0;
}

/* Connects the pipes: a producer's `feeds` entry reaches a loaded consumer whose
 * inbox has that type. Every slot's pipe block is rewritten in place. */
void drv_connect_pipes(mesh_driver *d) {
    mesh_mailbox mb = d->sh->mb;
    uint32_t feeds[MESH_PROG_SLOTS] = {0}, fed[MESH_PROG_SLOTS] = {0};
    for (uint32_t p = 0; p < MESH_PROG_SLOTS; p++) {
        dslot *x = d->slots[p];
        if (!x || x->unloading) continue;
        for (size_t f = 0; f < x->image.manifest.nfeeds; f++) {
            const mesh_manifest_feed *fd = &x->image.manifest.feeds[f];
            for (uint32_t c = 0; c < MESH_PROG_SLOTS; c++) {
                dslot *y = d->slots[c];
                if (y && !y->unloading && strcmp(drv_name(y), fd->consumer) == 0 && y->image.manifest.inbox &&
                    strcmp(y->image.manifest.inbox->ty, fd->ty) == 0) {
                    feeds[p] |= 1u << c;
                    fed[c] |= 1u << p;
                    break;
                }
            }
        }
    }
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (!x) continue;
        if (x->feeds_mask != feeds[s] || x->fed_mask != fed[s]) mesh_info("pipes: %s feeds 0x%x, fed by 0x%x", drv_name(x), feeds[s], fed[s]);
        x->feeds_mask = feeds[s];
        x->fed_mask = fed[s];
        uint8_t pp[MESH_PP_BYTES];
        drv_pipe(x, pp);
        mesh_pt_write_pipe(mb, s, pp);
    }
}

/* A pipe's two ends must agree on its record type: m against the loaded programs (except: the slot m replaces). */
bool drv_pipe_check(mesh_driver *d, const mesh_manifest *m, bool has_except, uint32_t except, char **err) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (!x || (has_except && except == s)) continue;
        const mesh_manifest *o = &x->image.manifest;
        for (size_t f = 0; f < m->nfeeds; f++) {
            const char *c = m->feeds[f].consumer, *ty = m->feeds[f].ty;
            if (strcmp(c, o->name) != 0) continue;
            if (o->inbox && strcmp(o->inbox->ty, ty) == 0) continue;
            if (o->inbox) *err = mesh_buf_fmt("manifest: %s feeds %s records of type %s, but %s's inbox carries %s", m->name, c, ty, c, o->inbox->ty);
            else *err = mesh_buf_fmt("manifest: %s feeds %s, which has no inbox", m->name, c);
            return false;
        }
        if (m->inbox) {
            for (size_t f = 0; f < o->nfeeds; f++) {
                if (strcmp(o->feeds[f].consumer, m->name) == 0 && strcmp(o->feeds[f].ty, m->inbox->ty) != 0) {
                    *err = mesh_buf_fmt("manifest: %s feeds %s records of type %s, but this inbox carries %s", o->name, m->name, o->feeds[f].ty, m->inbox->ty);
                    return false;
                }
            }
        }
    }
    return true;
}

/* The exchange page (every system's counter and slots) zeroed: only while the lockstep program is halted. */
void drv_zero_exchange(mesh_driver *d) {
    mesh_mailbox mb = d->sh->mb;
    for (uint32_t k = 0; k < MESH_MAX_SYSTEMS; k++) {
        mesh_mailbox_zero(mb, mesh_ex_slots(k), MESH_EX_REGION_BYTES);
        mesh_mailbox_publish64(mb, mesh_ex_counter(k), 0);
    }
}

/* ---------------------------------------------------------------- the http program's loads */

static void http_load(mesh_driver *d) {
    mesh_mailbox mb = d->sh->mb;
    if (d->gpu_load) {
        mesh_result r;
        int got = mesh_reply_poll(d->gpu_load_rx, &r);
        if (got == MESH_CHAN_EMPTY) return;
        if (got != MESH_CHAN_OK) { r = mesh_result_err("load: dropped"); }
        uint32_t s = d->gpu_load_slot, seq = d->gpu_load_seq;
        mesh_reply_release(d->gpu_load_rx);
        d->gpu_load = false;
        d->gpu_load_rx = NULL;
        /* Still the same http program?  Then answer it: 0 ok, else 1 + the reason's index. */
        dslot *x = d->slots[s];
        if (x && strcmp(drv_name(x), "http") == 0) {
            uint32_t code = MESH_HTTP_LOAD_OK;
            if (!r.ok) {
                code = 1 + 8;
                for (uint32_t i = 0; i < MESH_LOAD_REASONS_LEN; i++) {
                    size_t k = strlen(MESH_LOAD_REASONS[i]);
                    if (r.text && strncmp(r.text, MESH_LOAD_REASONS[i], k) == 0) { code = 1 + i; break; }
                }
            }
            mesh_mailbox_set(mb, (size_t)x->view_off + MESH_HTTP_HV_LOAD_RESULT, code);
            mesh_mailbox_publish(mb, (size_t)x->view_off + MESH_HTTP_HV_LOAD_ACK, seq);
        }
        mesh_result_free(&r);
        return;
    }
    int s = drv_find(d, "http");
    if (s < 0) return;
    dslot *x = d->slots[s];
    if (x->image.manifest.view_bytes < MESH_HTTP_HV_LOAD_NAME + 16) return;
    size_t v = x->view_off;
    uint32_t seq = mesh_mailbox_get(mb, v + MESH_HTTP_HV_LOAD_SEQ);
    if (seq == mesh_mailbox_get(mb, v + MESH_HTTP_HV_LOAD_ACK)) return;
    uint8_t b[17];
    mesh_mailbox_read(mb, v + MESH_HTTP_HV_LOAD_NAME, b, 16);
    b[16] = 0;
    mesh_buf name = MESH_BUF_INIT;
    mesh_rs_lossy(&name, b, strlen((char *)b));
    mesh_info("http: GET /load/%s", mesh_buf_str(&name));
    mesh_reply *rx = mesh_reply_new();
    /* In a fleet a load is a change of the desired state (the leader commits it, every member loads it). */
    if (d->sh->peer) {
        mesh_peer_event e = mesh_peer_event_new(MESH_PEV_CHANGE);
        e.line = mesh_buf_fmt("load %s", mesh_buf_str(&name));
        e.reply = mesh_reply_sender(rx);
        if (mesh_chan_send(d->sh->peer, &e) != MESH_CHAN_OK) mesh_peer_event_free(&e);
    } else {
        djob j = {false, mesh_src_catalog(mesh_buf_str(&name)), -1, 0, mesh_reply_sender(rx)};
        drv_push_job(d, j);
    }
    mesh_buf_free(&name);
    d->gpu_load = true;
    d->gpu_load_slot = (uint32_t)s;
    d->gpu_load_seq = seq;
    d->gpu_load_rx = rx;
}

/* P_RUN_REQ changed: a GPU program (http) asked to start/stop slot s. */
static void run_request(mesh_driver *d, uint32_t s) {
    mesh_mailbox mb = d->sh->mb;
    uint32_t req = mesh_mailbox_get(mb, ctl(s, MESH_P_RUN_REQ));
    dslot *x = d->slots[s];
    if (req >> 1 == x->run_seq >> 1) return;
    x->run_seq = req;
    bool on = (req & 1u) && !x->unloading;
    bool want = x->want_run;
    /* In a fleet the run state is the desired state's: the request goes through the leader. */
    if (d->sh->peer) {
        if (on != want) {
            mesh_info("run request: %s %s (a change of the desired state)", on ? "start" : "stop", drv_name(x));
            mesh_peer_event e = mesh_peer_event_new(MESH_PEV_CHANGE);
            e.line = mesh_buf_fmt("%s %s", on ? "start" : "stop", drv_name(x));
            if (mesh_chan_send(d->sh->peer, &e) != MESH_CHAN_OK) mesh_peer_event_free(&e);
        }
        return;
    }
    mesh_mailbox_set(mb, ctl(s, MESH_P_RUN), on);
    if (on != x->want_run) mesh_info("run request: %s %s", on ? "start" : "stop", drv_name(x));
    if (on) { free(x->failed); x->failed = NULL; }
    x->want_run = on;
    d->state_dirty = true;
}

/* ---------------------------------------------------------------- completions */

/* The lockstep program's progress for viewers: its next arg (u64 with carry) and view prefix, under one lock. */
static void lock_progress(mesh_driver *d, uint32_t s) {
    mesh_mailbox mb = d->sh->mb;
    dslot *x = d->slots[s];
    const mesh_manifest *m = &x->image.manifest;
    uint32_t lo = m->arg_mode == MESH_ARG_VIEW_U32 ? mesh_mailbox_get(mb, (size_t)x->view_off + m->arg_off) : mesh_mailbox_get(mb, ctl(s, MESH_P_DONE));
    if (lo < d->lock_lo) d->lock_hi++;
    d->lock_lo = lo;
    uint64_t step = (uint64_t)d->lock_hi << 32 | lo;
    uint8_t sb[8], view[32], name[MESH_NAME_BYTES];
    for (int i = 0; i < 8; i++) sb[i] = (uint8_t)(step >> (8 * i));
    memset(view, 0, sizeof view);
    mesh_mailbox_read(mb, x->view_off, view, m->view_bytes < 32 ? m->view_bytes : 32);
    memset(name, 0, sizeof name);
    memcpy(name, m->name, strlen(m->name) < MESH_NAME_BYTES ? strlen(m->name) : MESH_NAME_BYTES);
    mesh_host_field f[3] = {{MESH_S_LOCK_STEP, sb, 8}, {MESH_S_LOCK_VIEW, view, 32}, {MESH_S_LOCK_NAME, name, MESH_NAME_BYTES}};
    mesh_host_block_set_many(&d->sh->host, f, 3);
}

void drv_collect(mesh_driver *d, uint32_t s) {
    mesh_mailbox mb = d->sh->mb;
    dslot *x = d->slots[s];
    if (!drv_live(x)) return;
    mesh_done *done;
    size_t n;
    mesh_pring_collect(&x->ring, mb, &done, &n);
    bool lockstep = x->image.manifest.peer == MESH_PEER_MODE_LOCKSTEP;
    for (size_t i = 0; i < n; i++) {
        mesh_out *o = &done[i].out;
        uint32_t r = done[i].result;
        x = d->slots[s];
        switch (o->kind.tk) {
        case MESH_TK_AUTO:
            if (lockstep) lock_progress(d, s);
            break;
        case MESH_TK_FOLLOW: break;
        case MESH_TK_INIT: mesh_info("%s: OP_INIT done", drv_name(x)); break;
        case MESH_TK_JOB: {
            djobslot *j = &d->jobw[s];
            if (j->inflight) j->inflight--;
            if (r == MESH_R_JOB_FAIL) {
                char *why = mesh_buf_fmt("%s: item %llu: result 0x%x (R_JOB_FAIL)", drv_name(x), (unsigned long long)o->kind.item, r);
                drv_job_done(d, o->kind.lease, o->kind.job, o->kind.item, MESH_JOBS_RS_FAIL, r, why);
                free(why);
            } else {
                const mesh_manifest *m = &x->image.manifest;
                mesh_job_event e = mesh_job_event_new(MESH_JEV_DONE);
                e.res.lease = o->kind.lease;
                e.res.job = o->kind.job;
                e.res.item = o->kind.item;
                e.res.status = MESH_JOBS_RS_OK;
                e.res.result = r;
                /* The item's job_out record, at its completion slot (read before the slot's next ticket is published). */
                size_t slot = o->ticket % MESH_PROG_RESULTS;
                if (m->has_job_out) {
                    e.res.out_len = m->job_out_rec;
                    e.res.out = mesh_xmalloc(m->job_out_rec ? m->job_out_rec : 1);
                    mesh_mailbox_read(mb, (size_t)x->view_off + m->job_out_off + (size_t)m->job_out_rec * slot, e.res.out, m->job_out_rec);
                }
                /* Its payload (`job_payload`), at the same slot. */
                if (m->has_job_payload) {
                    e.has_payload = true;
                    e.payload_len = m->job_payload_len;
                    e.payload = mesh_xmalloc(m->job_payload_len ? m->job_payload_len : 1);
                    mesh_mailbox_read(mb, (size_t)x->view_off + m->job_payload_off + (size_t)m->job_payload_len * slot, e.payload, m->job_payload_len);
                }
                if (mesh_chan_send(d->sh->jobs, &e) != MESH_CHAN_OK) mesh_job_event_free(&e);
            }
            break;
        }
        case MESH_TK_JOB_SETUP: {
            char *err = NULL;
            if (r == MESH_R_BAD_OP) err = mesh_buf_fmt("%s: setup task %u %u -> R_BAD_OP", drv_name(x), o->kind.op, o->kind.arg);
            mesh_setup_state_completed(&d->jobw[s].setup, o->kind.job, err);
            free(err);
            break;
        }
        case MESH_TK_TASK: mesh_reply_value(o->kind.reply, r); o->kind.reply = NULL; break;
        case MESH_TK_REQ: {
            const mesh_manifest *m = &x->image.manifest;
            if (mesh_mailbox_get64(mb, ctl(s, MESH_P_RESP_SEQ)) == o->kind.seq) {
                uint32_t len = mesh_mailbox_get(mb, ctl(s, MESH_P_RESP_LEN));
                if (len > m->request.resp_max) len = m->request.resp_max;
                uint8_t *buf = mesh_xmalloc(len ? len : 1);
                mesh_mailbox_read(mb, (size_t)x->view_off + m->request.resp_off, buf, len);
                mesh_reply_bytes(o->kind.reply, buf, len);
                free(buf);
            } else {
                mesh_reply_errf(o->kind.reply, "%s: OP_REQ completed without a response to request %llu", drv_name(x), (unsigned long long)o->kind.seq);
            }
            o->kind.reply = NULL;
            break;
        }
        }
    }
    free(done);
}

/* ---------------------------------------------------------------- phases */

bool drv_halted(mesh_driver *d, uint32_t s, bool *halted, char **err) {
    dslot *x = d->slots[s];
    mesh_backend *ex = drv_be(d, x->image.exec);
    /* A native worker: halted (or exited, or dead) as its process says. */
    if (mesh_backend_owns_halt(ex)) return ex->ops->query(ex, x->stream, x->image.kernel, halted, err);
    if (mesh_mailbox_get(d->sh->mb, ctl(s, MESH_P_HALTED)) < x->image.manifest.nodes) { *halted = false; return true; }
    return ex->ops->query(ex, x->stream, x->image.kernel, halted, err);
}

void drv_resolve(mesh_driver *d, uint32_t s, bool on, bool ok, const char *text) {
    size_t k = 0;
    for (size_t i = 0; i < d->nwaiters; i++) {
        dwaiter *w = &d->waiters[i];
        if (w->slot != s || w->on != on || w->unload) { d->waiters[k++] = *w; continue; }
        drv_answer(w->reply, ok, text);
    }
    d->nwaiters = k;
}

void drv_running(mesh_driver *d, uint32_t s, int64_t t0) {
    dslot *x = d->slots[s];
    x->phase = PH_RUNNING;
    x->last_auto = mesh_mono_ns();
    long long us = (mesh_mono_ns() - t0) / 1000;
    mesh_info("%s running: %u nodes online in %lld us", drv_name(x), x->image.manifest.nodes, us);
    char t[32];
    snprintf(t, sizeof t, "%lld us", us);
    drv_resolve(d, s, true, true, t);
}

/* Tickets that will never run: their waiters get an error, an OP_INIT is due again, a job item goes back (or fails). */
void drv_not_run(mesh_driver *d, uint32_t s, mesh_out *outs, size_t n) {
    if (!n) { free(outs); return; }
    dslot *x = d->slots[s];
    char *why = mesh_buf_fmt("%s stopped before running it", drv_name(x));
    for (size_t i = 0; i < n; i++) {
        mesh_kind *k = &outs[i].kind;
        switch (k->tk) {
        case MESH_TK_INIT: x->needs_init = true; break;
        case MESH_TK_TASK:
        case MESH_TK_REQ: mesh_reply_err(k->reply, why); k->reply = NULL; break;
        case MESH_TK_JOB:
            if (x->failed) {
                char *e = mesh_buf_fmt("the program failed while it ran: %s", x->failed);
                drv_job_done(d, k->lease, k->job, k->item, MESH_JOBS_RS_FAIL, 0, e);
                free(e);
            } else drv_job_done(d, k->lease, k->job, k->item, MESH_JOBS_RS_RETURNED, 0, "the program stopped");
            break;
        default: break;
        }
    }
    free(why);
    free(outs);
}

void drv_stopped(mesh_driver *d, uint32_t s) {
    mesh_mailbox mb = d->sh->mb;
    drv_collect(d, s);
    drv_producer_stopped(d, s);
    dslot *x = d->slots[s];
    x->phase = PH_STOPPED;
    mesh_out *left;
    size_t nleft;
    mesh_pring_rewind(&x->ring, mb, &left, &nleft);
    drv_not_run(d, s, left, nleft);
    char *why = mesh_buf_fmt("%s stopped before running it", drv_name(x));
    bool stop = !x->want_run || x->failed || x->unloading;
    /* The program halted: its job setup is run again before the next item. */
    djobslot *j = &d->jobw[s];
    j->has_ctx = false;
    j->inflight = 0;
    mesh_setup_state_reset(&j->setup);
    if (stop) {
        size_t nq = j->n;
        dlease *q = j->q;
        j->q = NULL;
        j->n = j->cap = 0;
        for (size_t i = 0; i < nq; i++) {
            drv_jobs_returned(d, &q[i], MESH_JOBS_RS_RETURNED, "the program stopped");
            drv_lease_free(&q[i]);
        }
        free(q);
    }
    if (!x->want_run || x->failed) {
        for (size_t i = 0; i < x->ntasks; i++) mesh_reply_err(x->tasks[i].reply, why);
        x->ntasks = 0;
        for (size_t i = 0; i < x->nreqs; i++) { mesh_reply_err(x->reqs[i].reply, why); free(x->reqs[i].bytes); }
        x->nreqs = 0;
    }
    free(why);
    mesh_mailbox_publish(mb, ctl(s, MESH_P_SHUTDOWN), 0);
    mesh_pt_set_state(mb, s, drv_state(x));
    if (x->failed) {
        char *f = mesh_xstrdup(x->failed);
        drv_resolve(d, s, true, false, f);
        free(f);
    }
    if (x->unloading) drv_remove(d, s);
    else {
        for (size_t i = 0; i < d->nwaiters; i++) {
            dwaiter *w = &d->waiters[i];
            if (w->slot == s && !w->on && !w->unload) {
                char t[32];
                snprintf(t, sizeof t, "%lld us", (long long)((mesh_mono_ns() - w->t0) / 1000));
                drv_resolve(d, s, false, true, t);
                break;
            }
        }
    }
}

void drv_fail(mesh_driver *d, uint32_t s, char *why) {
    mesh_mailbox mb = d->sh->mb;
    dslot *x = d->slots[s];
    mesh_error("%s FAILED: %s", drv_name(x), why);
    free(x->failed);
    x->failed = why;
    switch (x->phase) {
    case PH_STOPPED: drv_stopped(d, s); break;
    case PH_HALTING: mesh_pt_set_state(mb, s, MESH_ST_FAILED); break;
    default:
        mesh_mailbox_publish(mb, ctl(s, MESH_P_SHUTDOWN), MESH_SHUTDOWN_EXIT);
        x->phase = PH_HALTING;
        x->phase_t0 = mesh_mono_ns();
        mesh_pt_set_state(mb, s, MESH_ST_FAILED);
    }
}

bool drv_launch_slot(mesh_driver *d, uint32_t s, char **err) {
    mesh_mailbox mb = d->sh->mb;
    dslot *x = d->slots[s];
    mesh_backend *be = drv_be(d, x->image.exec);
    bool idle = false;
    if (!be->ops->query(be, x->stream, x->image.kernel, &idle, err)) return false;
    if (!idle) { *err = mesh_xstrdup("its previous kernel is still on the stream"); return false; }
    /* stopped() rewound the ring already: anything outstanding now was never run. */
    mesh_out *left;
    size_t nleft;
    mesh_pring_rewind(&x->ring, mb, &left, &nleft);
    if (nleft) mesh_error("%s: %zu ticket(s) outstanding at launch (not run)", drv_name(x), nleft);
    drv_not_run(d, s, left, nleft);
    /* A relaunch after FAILED starts clean (P_FAULT keeps only the first fault). */
    mesh_mailbox_set(mb, ctl(s, MESH_P_FAULT), 0);
    mesh_mailbox_publish(mb, ctl(s, MESH_P_SHUTDOWN), 0);
    mesh_mailbox_fetch_add(mb, ctl(s, MESH_P_LAUNCHES), 1);
    if (!be->ops->launch(be, x->stream, x->image.kernel, x->image.manifest.nodes, mb.dev, s, err)) return false;
    x->phase = PH_STARTING;
    x->phase_t0 = mesh_mono_ns();
    bool init = x->needs_init;
    if (init) {
        /* First on the ring: every later task sees an initialised view. */
        x->needs_init = false;
        mesh_pring_publish(&x->ring, mb, MESH_OP_INIT, 0, mesh_kind_of(MESH_TK_INIT), NULL);
    }
    mesh_pt_set_state(mb, s, MESH_ST_RUNNING);
    /* A fresh view restarts the lockstep program's step. */
    if (init && x->image.manifest.peer == MESH_PEER_MODE_LOCKSTEP) d->lock_hi = d->lock_lo = 0;
    return true;
}

void drv_phase_step(mesh_driver *d, uint32_t s, int64_t now, bool in_blink) {
    uint32_t exec = drv_exec_of(d, s);
    bool gpu = exec == drv_gpu_kind(d);
    mesh_backend *be = drv_be(d, exec);
    dslot *x = d->slots[s];
    /* A native worker that died: FAILED with its exit status and output (nothing else stops). */
    if (drv_live(x)) {
        char *why = mesh_backend_crashed(be, x->stream, x->image.kernel);
        if (why) {
            if (x->failed) { mesh_error("%s: %s", drv_name(x), why); free(why); }
            else drv_fail(d, s, why);
            return;
        }
    }
    int64_t online = (int64_t)mesh_backend_online_timeout_ms(be) * 1000000;
    mesh_mailbox mb = d->sh->mb;
    uint32_t nodes = x->image.manifest.nodes;
    char *err = NULL;
    switch (x->phase) {
    case PH_STOPPED:
        /* A lost CUDA context stops only GPU slots. */
        if (x->want_run && !x->failed && !x->unloading && !in_blink && (!d->fault || !gpu)) {
            if (!drv_launch_slot(d, s, &err)) {
                char *why = mesh_buf_fmt("launch: %s", err);
                free(err);
                drv_fail(d, s, why);
            }
        }
        break;
    case PH_STARTING: {
        uint32_t on = mesh_mailbox_get(mb, ctl(s, MESH_P_ONLINE));
        if (on >= nodes) drv_running(d, s, x->phase_t0);
        else if (now - x->phase_t0 > online) {
            mesh_buf b = MESH_BUF_INIT;
            mesh_buf_printf(&b, "%u of %u nodes online after ", on, nodes);
            mesh_rs_duration_ms(&b, (uint64_t)(online / 1000000));
            drv_fail(d, s, mesh_buf_take(&b));
        }
        break;
    }
    case PH_RUNNING: {
        uint32_t f = mesh_mailbox_get(mb, ctl(s, MESH_P_FAULT));
        if (f != 0 && !x->failed) drv_fail(d, s, mesh_buf_fmt("fault %u (detail 0x%x)", f & 0xffu, f >> 8));
        else if (!x->want_run || x->unloading) {
            x->phase = PH_DRAINING;
            x->phase_t0 = now;
            mesh_pt_set_state(mb, s, MESH_ST_STOPPING);
        }
        break;
    }
    case PH_DRAINING:
        if (mesh_pring_idle(&x->ring) || now - x->phase_t0 > DRAIN_NS) {
            if (!mesh_pring_idle(&x->ring))
                mesh_error("%s: %zu task(s) did not drain in 3s; stopping anyway", drv_name(x), mesh_pring_in_flight(&x->ring));
            mesh_mailbox_publish(mb, ctl(s, MESH_P_SHUTDOWN), MESH_SHUTDOWN_EXIT);
            x->phase = PH_HALTING;
            x->phase_t0 = now;
        }
        break;
    case PH_HALTING: {
        bool failed = x->failed != NULL, h = false;
        if (!drv_halted(d, s, &h, &err)) { drv_context_fault(d, err); free(err); break; }
        if (h) drv_stopped(d, s);
        else if (now - x->phase_t0 > HALT_NS && !failed) {
            uint32_t hn = mesh_mailbox_get(mb, ctl(s, MESH_P_HALTED));
            drv_fail(d, s, mesh_buf_fmt("%u of %u nodes halted in 3s", hn, nodes));
        }
        break;
    }
    }
}

/* ---------------------------------------------------------------- unload */

void drv_remove(mesh_driver *d, uint32_t s) {
    dslot *x = d->slots[s];
    d->slots[s] = NULL;
    bool lockstep = x->image.manifest.peer == MESH_PEER_MODE_LOCKSTEP;
    uint32_t exec = x->image.exec;
    char *name = mesh_xstrdup(drv_name(x));
    uint32_t view_off = x->view_off, view_bytes = x->image.manifest.view_bytes;
    bool inbox = x->image.manifest.inbox != NULL;
    dzombie z = {NULL, view_off, view_bytes, exec, inbox, x->fed_mask, mesh_mono_ns(), false};
    if (exec == MESH_BACKEND_NATIVE) {
        /* A native image is no module: its processes exit now; only the view waits for a blink. */
        mesh_backend *ex = drv_be(d, exec);
        char *e = NULL;
        if (!ex->ops->unload(ex, x->image.kernel, &e)) free(e);
        x->image.kernel = NULL;
        if (x->has_prev) { if (!ex->ops->unload(ex, x->prev.kernel, &e)) free(e); x->prev.kernel = NULL; }
        mesh_slot_use *u = &mesh_cpuuse_slots[s];
        atomic_store_explicit(&u->rss, 0, memory_order_relaxed);
        atomic_store_explicit(&u->units, 0, memory_order_relaxed);
        d->native_cpu[s].key = 0;
        d->native_cpu[s].cpu = 0;
        PUSH(d->zombies, d->nzombies, d->czombies, z);
    } else {
        z.kernel = x->image.kernel;
        x->image.kernel = NULL;
        PUSH(d->zombies, d->nzombies, d->czombies, z);
        if (x->has_prev) {
            dzombie p = {x->prev.kernel, 0, 0, x->prev.exec, false, 0, mesh_mono_ns(), false};
            x->prev.kernel = NULL;
            PUSH(d->zombies, d->nzombies, d->czombies, p);
        }
    }
    char *gone = mesh_buf_fmt("%s was unloaded", name);
    for (size_t i = 0; i < x->ntasks; i++) mesh_reply_err(x->tasks[i].reply, gone);
    djobslot j = d->jobw[s];
    memset(&d->jobw[s], 0, sizeof d->jobw[s]);
    for (size_t i = 0; i < j.n; i++) { drv_jobs_returned(d, &j.q[i], MESH_JOBS_RS_RETURNED, "unloaded"); drv_lease_free(&j.q[i]); }
    free(j.q);
    mesh_setup_state_reset(&j.setup);
    for (size_t i = 0; i < x->nreqs; i++) { mesh_reply_err(x->reqs[i].reply, gone); free(x->reqs[i].bytes); }
    free(gone);
    mesh_pt_clear(d->sh->mb, s);
    size_t k = 0;
    for (size_t i = 0; i < d->ndead; i++) if (d->dead[i].consumer != s) d->dead[k++] = d->dead[i];
    d->ndead = k;
    drv_connect_pipes(d);
    if (lockstep) drv_zero_exchange(d);
    d->state_dirty = true;
    mesh_info("%s unloaded from slot %u (module and %zu view pages freed at the next blink)", name, s, mesh_view_pages(view_bytes));
    /* The slot's own resources. */
    drv_image_free(&x->image);
    if (x->has_prev) drv_image_free(&x->prev);
    mesh_stream_drop(x->stream);
    mesh_pring_free(&x->ring);
    free(x->tasks);
    free(x->reqs);
    free(x->failed);
    free(x);
    mesh_reply *now = NULL;
    mesh_reply *done[64];
    size_t ndone = 0;
    k = 0;
    for (size_t i = 0; i < d->nwaiters; i++) {
        dwaiter *w = &d->waiters[i];
        if (w->slot != s) { d->waiters[k++] = *w; continue; }
        if (w->unload && w->now) now = w->reply;
        else if (w->unload && ndone < 64) done[ndone++] = w->reply;
        else mesh_reply_errf(w->reply, "%s was unloaded", name);
    }
    d->nwaiters = k;
    for (size_t i = 0; i < ndone; i++) mesh_reply_ok(done[i], "");
    if (now) {
        uint32_t scope = 0;
        mesh_backend *be = drv_be(d, exec);
        bool has = be->ops->module_scope(be, false, 0, &scope);
        dblink b = {OP_FREE, NULL, -1, 0, now, has, scope, exec, mesh_mono_ns()};
        drv_push_blink(d, b);
    }
    free(name);
}

/* ---------------------------------------------------------------- publishing */

static void publish_slot(mesh_driver *d, uint32_t s, int64_t now) {
    mesh_mailbox mb = d->sh->mb;
    /* In a fleet the lockstep program is fed only while its group's epoch runs here. */
    bool lock_ok = !d->cfg.peered || atomic_load(&d->sh->lock_feed);
    dslot *x = d->slots[s];
    if (mesh_pring_has_init(&x->ring)) return;
    size_t t = 0;
    while (t < x->ntasks && mesh_pring_can_publish(&x->ring)) {
        mesh_kind k = mesh_kind_of(MESH_TK_TASK);
        k.reply = x->tasks[t].reply;
        if (!mesh_pring_publish(&x->ring, mb, x->tasks[t].op, x->tasks[t].arg, k, NULL)) break;
        t++;
    }
    if (t) { memmove(x->tasks, x->tasks + t, (x->ntasks - t) * sizeof *x->tasks); x->ntasks -= t; }
    if (!mesh_pring_has_req(&x->ring) && mesh_pring_can_publish(&x->ring) && x->nreqs) {
        dreq r = x->reqs[0];
        memmove(x->reqs, x->reqs + 1, (x->nreqs - 1) * sizeof *x->reqs);
        x->nreqs--;
        const mesh_manifest_window *w = &x->image.manifest.request;
        /* Word writes: never past req_max (checked at the request). */
        size_t span = (r.len + 3) / 4 * 4;
        if (span > w->req_max) abort();
        mesh_mailbox_write(mb, (size_t)x->view_off + w->req_off, r.bytes, r.len, span);
        mesh_mailbox_set(mb, ctl(s, MESH_P_REQ_LEN), (uint32_t)r.len);
        uint64_t seq = mesh_mailbox_get64(mb, ctl(s, MESH_P_REQ_SEQ)) + 1;
        mesh_mailbox_publish64(mb, ctl(s, MESH_P_REQ_SEQ), seq);
        mesh_kind k = mesh_kind_of(MESH_TK_REQ);
        k.seq = seq;
        k.reply = r.reply;
        mesh_pring_publish(&x->ring, mb, MESH_OP_REQ, 0, k, NULL);
        free(r.bytes);
    }
    if (x->image.manifest.policy == MESH_POLICY_JOB && x->want_run && x->phase == PH_RUNNING) { drv_publish_jobs(d, s); return; }
    const mesh_manifest *m = &x->image.manifest;
    if (x->want_run && (m->peer != MESH_PEER_MODE_LOCKSTEP || lock_ok))
        mesh_publish_auto(mb, m, x->view_off, &x->ring, &x->last_auto, now);
}

void drv_programs_tick(mesh_driver *d, int64_t now) {
    bool blink = d->nblinks > 0;
    bool has_scope = blink && d->blinks[0].has_scope;
    uint32_t scope = blink ? d->blinks[0].scope : 0;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (!d->slots[s]) continue;
        run_request(d, s);
        drv_collect(d, s);
        bool in_blink = blink && drv_in_scope(d, has_scope, scope, s);
        drv_phase_step(d, s, now, in_blink);
        drv_native_words(d, s);
        dslot *x = d->slots[s];
        if (!x) continue;
        if (!in_blink && (x->phase == PH_RUNNING || x->phase == PH_STARTING) && !x->failed) publish_slot(d, s, now);
    }
    http_load(d);
    dead_cells_tick(d);
}
