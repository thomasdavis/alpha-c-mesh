/* The job nodes against the Rust, output for output: a deterministic fleet under a scripted view (the
 * lowest-id member up leads while a majority is up; a new leader is a new term), the identical harness
 * to the Rust's tests/gold_trace.rs. Every output of every node is hashed (frames and log records by
 * their wire bytes); every 500 ms the hashes, counts and committed tables must equal the Rust's. */
#include "mesh/jobs/node.h"
#include "mesh/jobs/sim_fnv.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/check.h"
#include "mesh/ref/sha256d.h"
#include "gold_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the harness's random numbers (xorshift, as the Rust's Rng) ------------------------------- */

static uint64_t rs;
static uint64_t rnd(uint64_t n) {
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs % (n ? n : 1);
}

/* ---- canonical outputs ------------------------------------------------------------------------- */

static void canon(const mj_out *o, msb *v) {
    switch (o->kind) {
    case MJ_OUT_SEND:
        msb_putc(v, 'S');
        msb_put(v, &o->to, 4);
        mj_frame_encode(&o->frame, v);
        break;
    case MJ_OUT_PUSH_BLOB:
        msb_putc(v, 'P');
        msb_put(v, &o->to, 4);
        msb_put(v, &o->job, 8);
        msb_put(v, o->hash.b, 32);
        break;
    case MJ_OUT_FETCH_BLOB:
        msb_putc(v, 'F');
        msb_put(v, &o->job, 8);
        msb_put(v, o->hash.b, 32);
        msb_put(v, &o->holders, 4);
        break;
    case MJ_OUT_BLOB_DONE:
        msb_putc(v, 'D');
        msb_put(v, &o->job, 8);
        msb_put(v, o->hash.b, 32);
        break;
    case MJ_OUT_PERSIST:
        msb_putc(v, 'W');
        mj_log_encode(&o->log, v);
        break;
    case MJ_OUT_RUN: {
        msb_putc(v, 'R');
        mj_frame f = {.ty = MESH_F_LEASE, .lid = 0, .l = o->run};
        mj_frame_encode(&f, v);
        break;
    }
    case MJ_OUT_CUT:
        msb_putc(v, 'C');
        msb_put(v, &o->lease, 8);
        msb_put(v, &o->cut, 8);
        break;
    case MJ_OUT_ANSWER:
        msb_putc(v, 'A');
        msb_put(v, &o->token, 8);
        msb_putc(v, o->ok ? 1 : 0);
        msb_puts(v, o->text ? o->text : "");
        break;
    case MJ_OUT_LOG:
        msb_putc(v, 'L');
        msb_puts(v, o->text ? o->text : "");
        break;
    }
}

/* ---- the simulated fleet ----------------------------------------------------------------------- */

typedef struct {
    mj_job_id job;
    mj_hash h;
} ref;

typedef struct {
    ref *v;
    size_t n, cap;
} refs; /* kept sorted by (job, hash bytes), as a BTreeSet */

static int ref_cmp(const ref *a, const ref *b) {
    if (a->job != b->job) return a->job < b->job ? -1 : 1;
    return memcmp(a->h.b, b->h.b, 32);
}
static bool refs_has_hash(const refs *r, const mj_hash *h) {
    for (size_t i = 0; i < r->n; i++)
        if (mj_hash_eq(&r->v[i].h, h)) return true;
    return false;
}
static void refs_add(refs *r, mj_job_id j, const mj_hash *h) {
    ref x = {j, *h};
    size_t at = 0;
    while (at < r->n && ref_cmp(&r->v[at], &x) < 0) at++;
    if (at < r->n && ref_cmp(&r->v[at], &x) == 0) return;
    if (r->n == r->cap) {
        r->cap = r->cap ? 2 * r->cap : 64;
        r->v = mxrealloc(r->v, r->cap * sizeof *r->v);
    }
    memmove(&r->v[at + 1], &r->v[at], (r->n - at) * sizeof *r->v);
    r->v[at] = x;
    r->n++;
}
static void refs_del(refs *r, mj_job_id j, const mj_hash *h) {
    ref x = {j, *h};
    for (size_t i = 0; i < r->n; i++)
        if (ref_cmp(&r->v[i], &x) == 0) {
            memmove(&r->v[i], &r->v[i + 1], (r->n - i - 1) * sizeof *r->v);
            r->n--;
            return;
        }
}

typedef struct {
    uint64_t lease;
    mj_job_id job;
    mj_ranges items;
} qent;

typedef struct {
    mj_node *node;
    bool up;
    qent *q;
    size_t nq, capq;
    double speed, acc;
    msb disk;
    bool has_poison;
    uint64_t poison;
    bool bad_disk, crash_after;
    uint64_t hash, outs;
    refs refs, mem;
} member;

typedef enum { W_J, W_PUT, W_ACK, W_GIVE } wkind;
typedef struct {
    uint64_t at;
    uint32_t from, to;
    wkind kind;
    mj_frame f;
    mj_job_id job;
    mj_hash h;
    bool ok;
} wire;

typedef struct {
    uint32_t n;
    member m[8];
    uint64_t now, unix_ms;
    wire *w;
    size_t nw, capw;
    uint64_t last[8][8];
    uint64_t drop;
    uint64_t term;
    bool has_leader;
    uint32_t leader;
    uint64_t token;
    bool payload;
    msb out;
} sim;

static mj_cfg cfg_of(const sim *s, uint32_t k) { return (mj_cfg){k, s->n, s->n <= 2 ? 1 : s->n / 2 + 1, 3000}; }

static uint32_t up_mask(const sim *s) {
    uint32_t m = 0;
    for (uint32_t k = 0; k < s->n; k++)
        if (s->m[k].up) m |= (uint32_t)1 << k;
    return m;
}

static unsigned ones(uint32_t x) {
    unsigned c = 0;
    for (; x; x &= x - 1) c++;
    return c;
}

static void elect(sim *s) {
    uint32_t up = up_mask(s);
    bool want_some = false;
    uint32_t want = 0;
    if (s->n == 2) {
        if (up & 1) want_some = true, want = 0;
    } else if (ones(up) >= cfg_of(s, 0).quorum) {
        for (uint32_t k = 0; k < s->n; k++)
            if (up >> k & 1) {
                want_some = true;
                want = k;
                break;
            }
    }
    bool same = want_some == s->has_leader && (!want_some || want == s->leader);
    if (!same) {
        if (want_some && (s->n > 2 || s->term == 0)) s->term++;
        s->has_leader = want_some;
        s->leader = want;
    }
}

static mj_view view_of(const sim *s, uint32_t k) {
    uint32_t up = up_mask(s);
    return (mj_view){s->term > 1 ? s->term : 1, s->has_leader, s->leader, up, up | (uint32_t)1 << k};
}

static bool conn(const sim *s, uint32_t a, uint32_t b) { return s->m[a].up && s->m[b].up; }

static void put(sim *s, uint32_t from, uint32_t to, wire w) {
    if (!conn(s, from, to)) {
        mj_frame_free(&w.f);
        return;
    }
    if (s->drop > 0 && w.kind == W_J && rnd(1000) < s->drop) {
        mj_frame_free(&w.f);
        return;
    }
    uint64_t last = s->last[from][to];
    uint64_t at = s->now + 1 + rnd(4);
    if (last > at) at = last;
    s->last[from][to] = at;
    w.at = at;
    w.from = from;
    w.to = to;
    if (s->nw == s->capw) {
        s->capw = s->capw ? 2 * s->capw : 256;
        s->w = mxrealloc(s->w, s->capw * sizeof *s->w);
    }
    s->w[s->nw++] = w;
}

static void job_out(sim *s, uint32_t k, mj_outs outs);

static bool store(sim *s, uint32_t k, mj_job_id job, mj_hash h) {
    if (!s->m[k].up || mj_table_forgot(&s->m[k].node->rep.committed, job)) return false;
    refs_add(&s->m[k].refs, job, &h);
    mj_outs o = mj_node_blob_stored(s->m[k].node, job, h, s->now, s->unix_ms);
    job_out(s, k, o);
    return true;
}

static bool has_bytes(const sim *s, uint32_t k, const mj_hash *h) { return refs_has_hash(&s->m[k].refs, h) || refs_has_hash(&s->m[k].mem, h); }

static void crash(sim *s, uint32_t k) {
    member *m = &s->m[k];
    m->up = false;
    for (size_t i = 0; i < m->nq; i++) mj_ranges_free(&m->q[i].items);
    m->nq = 0;
    m->mem.n = 0;
    elect(s);
}

typedef struct {
    msb *buf;
    bool bad;
    bool *crash;
} disk1;

static bool d_append(void *c, const mj_log *l, char **err) {
    disk1 *d = c;
    if (d->bad) {
        *err = mstrdup("bad");
        return false;
    }
    mj_log_encode(l, d->buf);
    if (*d->crash) {
        *d->crash = false;
        *err = mstrdup("crash");
        return false;
    }
    return true;
}
static bool d_sync(void *c, char **err) {
    (void)c;
    (void)err;
    return true;
}

static void job_out(sim *s, uint32_t k, mj_outs outs) {
    if (!s->m[k].up) {
        mj_outs_free(&outs);
        return;
    }
    member *m = &s->m[k];
    for (size_t i = 0; i < outs.n; i++) {
        msb c = {0};
        canon(&outs.v[i], &c);
        mjs_fnv_into(&m->hash, c.p, c.n);
        m->outs++;
        msb_free(&c);
    }
    disk1 d = {&m->disk, m->bad_disk, &m->crash_after};
    mj_disk dk = {&d, d_append, d_sync};
    bool ok;
    char *err = NULL;
    mj_outs v = mj_durable(&outs, &dk, &ok, &err);
    free(err);
    if (!ok) {
        mj_outs_free(&v);
        crash(s, k);
        return;
    }
    for (size_t i = 0; i < v.n; i++) {
        mj_out *o = &v.v[i];
        switch (o->kind) {
        case MJ_OUT_SEND:
            put(s, k, o->to, (wire){.kind = W_J, .f = o->frame});
            o->frame = (mj_frame){0};
            break;
        case MJ_OUT_RUN: {
            member *mm = &s->m[k];
            if (mm->nq == mm->capq) {
                mm->capq = mm->capq ? 2 * mm->capq : 16;
                mm->q = mxrealloc(mm->q, mm->capq * sizeof *mm->q);
            }
            mm->q[mm->nq++] = (qent){o->run.lease, o->run.job, o->run.items};
            o->run.items = (mj_ranges){0};
            break;
        }
        case MJ_OUT_CUT:
            for (size_t q = 0; q < s->m[k].nq; q++)
                if (s->m[k].q[q].lease == o->lease) mj_ranges_remove(&s->m[k].q[q].items, o->cut, UINT64_MAX);
            break;
        case MJ_OUT_PUSH_BLOB:
            if (has_bytes(s, k, &o->hash)) put(s, k, o->to, (wire){.kind = W_PUT, .job = o->job, .h = o->hash});
            break;
        case MJ_OUT_FETCH_BLOB: {
            if (has_bytes(s, k, &o->hash)) {
                store(s, k, o->job, o->hash);
                break;
            }
            int from = -1;
            for (uint32_t j = 0; j < s->n && from < 0; j++)
                if ((o->holders >> j & 1) && j != k && conn(s, k, j) && has_bytes(s, j, &o->hash)) from = (int)j;
            for (uint32_t j = 0; j < s->n && from < 0; j++)
                if (j != k && conn(s, k, j) && has_bytes(s, j, &o->hash)) from = (int)j;
            if (from >= 0) put(s, (uint32_t)from, k, (wire){.kind = W_GIVE, .job = o->job, .h = o->hash});
            break;
        }
        case MJ_OUT_BLOB_DONE: refs_del(&s->m[k].mem, o->job, &o->hash); break;
        default: break;
        }
    }
    mj_outs_free(&v);
}

static void sim_init(sim *s, uint32_t n, uint64_t seed, bool payload) {
    memset(s, 0, sizeof *s);
    s->n = n;
    s->now = 1;
    s->unix_ms = 1700000000000u;
    rs = seed | 1;
    s->payload = payload;
    for (uint32_t k = 0; k < n; k++) {
        mj_replica r;
        mj_replica_init(&r);
        s->m[k].node = mj_node_new(cfg_of(s, k), r);
        s->m[k].up = true;
        s->m[k].speed = 1.0;
        s->m[k].hash = 0xcbf29ce484222325u;
    }
    elect(s);
}

static void sim_free(sim *s) {
    for (uint32_t k = 0; k < s->n; k++) {
        member *m = &s->m[k];
        mj_node_free(m->node);
        for (size_t i = 0; i < m->nq; i++) mj_ranges_free(&m->q[i].items);
        free(m->q);
        msb_free(&m->disk);
        free(m->refs.v);
        free(m->mem.v);
    }
    for (size_t i = 0; i < s->nw; i++) mj_frame_free(&s->w[i].f);
    free(s->w);
    msb_free(&s->out);
}

static void restart(sim *s, uint32_t k) {
    member *m = &s->m[k];
    mj_replica rep;
    if (!mj_replay(m->disk.p, m->disk.n, &rep, NULL, NULL, NULL)) abort();
    mj_node_free(m->node);
    m->node = mj_node_new(cfg_of(s, k), rep);
    m->up = true;
    m->bad_disk = false;
    m->crash_after = false;
    m->acc = 0.0;
    elect(s);
    refs copy = {0};
    for (size_t i = 0; i < m->refs.n; i++) refs_add(&copy, m->refs.v[i].job, &m->refs.v[i].h);
    for (size_t i = 0; i < copy.n; i++) store(s, k, copy.v[i].job, copy.v[i].h);
    free(copy.v);
}

static void progs(sim *s, const uint32_t *ks, size_t n, uint32_t cap) {
    for (size_t i = 0; i < n; i++) {
        mj_prog *p = mxalloc(sizeof *p);
        *p = (mj_prog){mstrdup("p"), 0, cap};
        mj_node_set_progs(s->m[ks[i]].node, p, 1);
    }
}

static uint64_t ask(sim *s, uint32_t k, const char *words) {
    s->token++;
    char *line = mstrdup(words);
    const char *w[64];
    size_t n = 0;
    for (char *t = strtok(line, " "); t && n < 64; t = strtok(NULL, " ")) w[n++] = t;
    mj_req r;
    if (!mj_req_parse(w, n, &r, NULL)) abort();
    free(line);
    mj_outs o = mj_node_request(s->m[k].node, r, s->token, s->now, s->unix_ms);
    job_out(s, k, o);
    return s->token;
}

static void record(sim *s) {
    msb_printf(&s->out, "t %llu term %llu leader ", (unsigned long long)s->now, (unsigned long long)s->term);
    if (s->has_leader) msb_printf(&s->out, "Some(%u)", s->leader);
    else msb_puts(&s->out, "None");
    for (uint32_t k = 0; k < s->n; k++) {
        member *m = &s->m[k];
        mj_ops ops = {0};
        mj_table_ops(&m->node->rep.committed, &ops);
        msb t = {0};
        mj_encode_ops(ops.v, ops.n, &t);
        msb_printf(&s->out, " | %u %u %016llx %llu %016llx %zu", k, m->up ? 1u : 0u, (unsigned long long)m->hash, (unsigned long long)m->outs,
                   (unsigned long long)mjs_fnv(t.p, t.n), m->node->rep.npending);
        msb_free(&t);
        mj_ops_free(&ops);
    }
    msb_putc(&s->out, '\n');
}

static int by_at(const void *a, const void *b) {
    const wire *x = a, *y = b;
    return x->at < y->at ? -1 : x->at > y->at ? 1 : 0;
}

static void step(sim *s, uint64_t ms) {
    uint64_t end = s->now + ms;
    while (s->now < end) {
        s->now++;
        s->unix_ms++;
        /* Frames due now, in delivery-time order (stable: the order they were sent in). */
        wire *due = NULL;
        size_t nd = 0, keep = 0;
        for (size_t i = 0; i < s->nw; i++) {
            if (s->w[i].at <= s->now) {
                due = mxrealloc(due, (nd + 1) * sizeof *due);
                due[nd] = s->w[i];
                due[nd].at = s->w[i].at * 1000000 + nd; /* a stable sort */
                nd++;
            } else s->w[keep++] = s->w[i];
        }
        s->nw = keep;
        if (nd) qsort(due, nd, sizeof *due, by_at);
        for (size_t i = 0; i < nd; i++) {
            wire *w = &due[i];
            if (!conn(s, w->from, w->to)) {
                mj_frame_free(&w->f);
                continue;
            }
            switch (w->kind) {
            case W_J: {
                mj_outs o = mj_node_recv(s->m[w->to].node, w->from, w->f, s->now, s->unix_ms);
                job_out(s, w->to, o);
                break;
            }
            case W_PUT: {
                bool ok = store(s, w->to, w->job, w->h);
                put(s, w->to, w->from, (wire){.kind = W_ACK, .job = w->job, .h = w->h, .ok = ok});
                break;
            }
            case W_ACK: mj_node_blob_acked(s->m[w->to].node, w->from, w->job, w->h, w->ok); break;
            case W_GIVE: store(s, w->to, w->job, w->h); break;
            }
        }
        free(due);
        for (uint32_t k = 0; k < s->n; k++) {
            member *m = &s->m[k];
            if (!m->up) continue;
            m->acc += m->speed;
            mj_item_res *res = NULL;
            size_t nres = 0;
            while (m->acc >= 1.0) {
                if (!m->nq) {
                    m->acc = 0.0;
                    break;
                }
                qent *q = &m->q[0];
                uint64_t x;
                if (!mj_ranges_first(&q->items, &x)) {
                    mj_ranges_free(&q->items);
                    memmove(m->q, m->q + 1, (m->nq - 1) * sizeof *m->q);
                    m->nq--;
                    continue;
                }
                mj_ranges_take(&q->items, x);
                m->acc -= 1.0;
                bool fail = m->has_poison && m->poison == x;
                mj_item_res r = {.lease = q->lease, .job = q->job, .item = x, .status = fail ? MESH_JOBS_RS_FAIL : MESH_JOBS_RS_OK,
                                 .result = fail ? 0 : (uint32_t)(x * 7 + 1)};
                if (fail) {
                    r.out = (uint8_t *)mstrdup("poison");
                    r.out_len = 6;
                }
                if (s->payload && !fail) {
                    uint8_t b[64];
                    memset(b, 0x5a, sizeof b);
                    memcpy(b, &x, 8);
                    r.has_blob = true;
                    mesh_sha256(b, 64, r.blob.hash.b);
                    r.blob.len = 64;
                }
                res = mxrealloc(res, (nres + 1) * sizeof *res);
                res[nres++] = r;
            }
            for (size_t i = 0; i < nres; i++) {
                mj_job_id job = res[i].job;
                bool hb = res[i].has_blob;
                mj_hash h = res[i].blob.hash;
                if (hb) refs_add(&s->m[k].mem, job, &h);
                mj_outs o = mj_node_result(s->m[k].node, res[i], s->now);
                job_out(s, k, o);
                if (hb && !store(s, k, job, h) && s->m[k].up) mj_node_blob_acked(s->m[k].node, k, job, h, false);
            }
            free(res);
        }
        if (s->now % 5 == 0)
            for (uint32_t k = 0; k < s->n; k++) {
                if (!s->m[k].up) continue;
                mj_outs o = mj_node_set_view(s->m[k].node, view_of(s, k), s->now, s->unix_ms);
                job_out(s, k, o);
                o = mj_node_tick(s->m[k].node, s->now, s->unix_ms);
                job_out(s, k, o);
            }
        if (s->now % 500 == 0) record(s);
    }
}

static void scenario(int id, msb *all) {
    sim s;
    switch (id) {
    case 0: {
        sim_init(&s, 1, 3, false);
        progs(&s, (uint32_t[]){0}, 1, 8);
        step(&s, 50);
        ask(&s, 0, "submit p items 3000");
        step(&s, 1000);
        ask(&s, 0, "submit p items 3,10-20,40 results none");
        s.m[0].speed = 0.3;
        ask(&s, 0, "submit p items 2000 priority 0");
        step(&s, 200);
        ask(&s, 0, "submit p items 200 priority 9");
        step(&s, 1500);
        crash(&s, 0);
        step(&s, 100);
        restart(&s, 0);
        progs(&s, (uint32_t[]){0}, 1, 8);
        s.m[0].speed = 1.0;
        step(&s, 3000);
        mj_outs o = mj_node_result(s.m[0].node,
                                   (mj_item_res){.lease = 12345, .job = (uint64_t)1 << 32 | 1, .item = 5, .status = MESH_JOBS_RS_OK, .result = 1}, s.now);
        job_out(&s, 0, o);
        ask(&s, 0, "submit p items 100000 deadline 1");
        step(&s, 1500);
        ask(&s, 0, "submit p items 0- results none");
        step(&s, 500);
        ask(&s, 0, "cancel 1.7");
        step(&s, 500);
        ask(&s, 0, "forget 1.7");
        ask(&s, 0, "forget 1.6");
        ask(&s, 0, "tone 1.1 reinhard");
        ask(&s, 0, "submit q items 10");
        step(&s, 3000);
        break;
    }
    case 1: {
        sim_init(&s, 3, 11, false);
        progs(&s, (uint32_t[]){0, 1, 2}, 3, 8);
        for (int k = 0; k < 3; k++) {
            s.m[k].has_poison = true;
            s.m[k].poison = 77;
        }
        step(&s, 400);
        ask(&s, 0, "submit p items 20000 attempts 3");
        step(&s, 1500);
        s.m[2].speed = 0.05;
        step(&s, 1500);
        crash(&s, 0);
        step(&s, 4000);
        restart(&s, 0);
        progs(&s, (uint32_t[]){0}, 1, 8);
        step(&s, 2000);
        s.m[2].speed = 1.0;
        ask(&s, s.leader, "submit p items 3000 results nonzero");
        step(&s, 6000);
        break;
    }
    case 2: {
        sim_init(&s, 5, 21, true);
        progs(&s, (uint32_t[]){0, 1, 2, 3, 4}, 5, 8);
        step(&s, 300);
        ask(&s, 0, "submit p items 6000");
        step(&s, 800);
        crash(&s, 2);
        step(&s, 600);
        s.drop = 30;
        crash(&s, 0);
        step(&s, 3000);
        s.drop = 0;
        restart(&s, 2);
        progs(&s, (uint32_t[]){2}, 1, 8);
        restart(&s, 0);
        progs(&s, (uint32_t[]){0}, 1, 8);
        step(&s, 6000);
        break;
    }
    case 4: {
        sim_init(&s, 3, 31, false);
        progs(&s, (uint32_t[]){0, 1, 2}, 3, 8);
        step(&s, 400);
        ask(&s, 0, "submit p items 9000 attempts 2");
        step(&s, 1000);
        s.m[1].speed = 0.0;
        step(&s, 12000);
        s.m[1].speed = 1.0;
        s.unix_ms -= 600000;
        crash(&s, 2);
        step(&s, 2000);
        restart(&s, 2);
        progs(&s, (uint32_t[]){2}, 1, 8);
        step(&s, 5000);
        break;
    }
    case 5: {
        sim_init(&s, 5, 41, false);
        progs(&s, (uint32_t[]){0, 1, 2, 3, 4}, 5, 8);
        step(&s, 300);
        ask(&s, 0, "submit p items 20000");
        s.drop = 200;
        step(&s, 3000);
        s.drop = 0;
        crash(&s, 3);
        step(&s, 2000);
        restart(&s, 3);
        progs(&s, (uint32_t[]){3}, 1, 8);
        step(&s, 1000);
        s.m[0].crash_after = true;
        step(&s, 4000);
        restart(&s, 0);
        progs(&s, (uint32_t[]){0}, 1, 8);
        step(&s, 5000);
        break;
    }
    case 6: {
        sim_init(&s, 3, 61, true);
        progs(&s, (uint32_t[]){0, 1, 2}, 3, 8);
        step(&s, 300);
        ask(&s, 0, "submit p items 1500");
        step(&s, 3000);
        ask(&s, 0, "submit p items 1500");
        step(&s, 1500);
        crash(&s, 0);
        step(&s, 3000);
        uint32_t l = s.leader;
        ask(&s, l, "forget 1.1");
        ask(&s, l, "submit p items 2000");
        step(&s, 500);
        ask(&s, l, "cancel 2.1");
        step(&s, 1500);
        restart(&s, 0);
        progs(&s, (uint32_t[]){0}, 1, 8);
        step(&s, 4000);
        break;
    }
    default: {
        sim_init(&s, 2, 51, false);
        progs(&s, (uint32_t[]){0, 1}, 2, 8);
        step(&s, 300);
        ask(&s, 0, "submit p items 8000");
        step(&s, 1000);
        s.m[0].crash_after = true;
        step(&s, 300);
        restart(&s, 0);
        progs(&s, (uint32_t[]){0}, 1, 8);
        step(&s, 1000);
        s.m[1].bad_disk = true;
        step(&s, 500);
        restart(&s, 1);
        progs(&s, (uint32_t[]){1}, 1, 8);
        step(&s, 6000);
    }
    }
    msb_printf(all, "scenario %d\n", id);
    msb_puts(all, msb_str(&s.out));
    sim_free(&s);
}

int main(void) {
    msb all = {0};
    for (int id = 0; id < 7; id++) scenario(id, &all);
    size_t g = 0, ng = sizeof GOLD / sizeof *GOLD, first_bad = SIZE_MAX;
    char *text = msb_take(&all);
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n"), g++) {
        if (g >= ng || strcmp(line, GOLD[g]) != 0) {
            if (first_bad == SIZE_MAX) {
                first_bad = g;
                fprintf(stderr, "first difference at line %zu:\n  C:    %s\n  Rust: %s\n", g, line, g < ng ? GOLD[g] : "(none)");
            }
        }
    }
    CHECK_U(first_bad, SIZE_MAX);
    CHECK_U(g, ng);
    free(text);
    return mesh_check_done();
}
