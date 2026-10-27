/* jobs.rs's simulated fleet (its tests' `Sim`), ported: members each with the fleet's election core
 * (@ajax/mesh-fleet-core), a job node, a fake executor, a log on a "disk" that can fail, a blob
 * store in memory (it survives a crash, as its files would) and blobd's memory of its own payloads;
 * links that are FIFO and can drop job frames. Every 200 ms each member's committed results are
 * checked: a committed result never changes, every holder a payload result names holds the job's own
 * copy, and the member's log replays to its state. */
#ifndef FSIM_H
#define FSIM_H

#include "mesh/assemble.h"
#include "mesh/blobs/store.h"
#include "mesh/check.h"
#include "mesh/fleet_core.h"
#include "mesh/ftz.h"
#include "mesh/jobs/node.h"
#include "mesh/omap.h"
#include "mesh/ref/pathtrace.h"
#include "mesh/ref/sha256d.h"
#include "mesh/strbuf.h"
#include "pathtrace/scene.h"
#include "pathtrace/view.h"
#include "pathtrace/z.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NMAX 8

typedef struct {
    uint64_t lease;
    mj_job_id job;
    mj_ranges items;
} fqe;

typedef struct {
    uint64_t lease, n;
} fgot;

typedef struct {
    mj_job_id *v;
    size_t n;
} fjobs; /* a sorted set of job ids */

typedef struct {
    mfc_core *core; /* NULL: a single meshd */
    mfp_persist fdisk;
    mj_node *node;
    bool up, mute;
    fqe *q;
    size_t nq, capq;
    double speed, acc;
    msb disk;
    bool has_poison;
    uint64_t poison;
    fgot *got;
    size_t ngot;
    uint64_t ran;
    bool bad_disk, crash_after_persist, has_crash_item;
    uint64_t crash_item;
    size_t base;
    mb_store *blobs;
    momap *mem;  /* hash words -> fjobs: its own payloads until their copies are durable */
    momap *told; /* forgotten jobs blobd was told of */
    uint64_t refused;
    /* What the disk replays to, kept up to date record by record (rebuilt after a rewrite). */
    bool chk_valid;
    mj_replica chk;
    size_t chk_at;
} fm;

typedef enum { FW_F, FW_J, FW_PUT, FW_ACK, FW_GIVE } fwk;
typedef struct {
    uint64_t at;
    uint32_t from, to;
    fwk kind;
    mfc_msg msg;
    mj_frame f;
    mj_job_id job;
    mj_hash h;
    bool ok;
} fw;

typedef struct {
    bool ok;
    char *text;
} fanswer;

typedef struct {
    uint8_t *b;
    size_t n;
    uint64_t digest;
} fpay;

typedef struct {
    uint32_t n;
    fm m[NMAX];
    uint64_t now, unix_ms;
    fw *w;
    size_t nw, capw;
    uint64_t rng;
    momap *answers; /* token -> fanswer */
    uint64_t token;
    momap *seen;    /* (job, item) -> uint32_t result */
    uint64_t last[NMAX][NMAX];
    char **logs;
    size_t nlogs, caplogs;
    uint64_t drop, dropped;
    uint32_t fail_stops;
    size_t max_pending;
    bool payload, salt;
    fpay *real; /* real pathtrace pass sums and digests per item, or NULL */
    size_t nreal;
    momap *bytes; /* hash words -> fpay: every payload's bytes */
    uint64_t caps[NMAX];
    uint32_t min_copies;
} fsim;

static inline uint64_t fs_rand(fsim *s, uint64_t n) {
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 7;
    s->rng ^= s->rng << 17;
    return s->rng % (n ? n : 1);
}

static inline mj_cfg fs_cfg(const fsim *s, uint32_t k) { return (mj_cfg){k, s->n, s->n <= 2 ? 1 : s->n / 2 + 1, 3000}; }

static inline void fjobs_add(fjobs *j, mj_job_id id) {
    size_t at = 0;
    while (at < j->n && j->v[at] < id) at++;
    if (at < j->n && j->v[at] == id) return;
    j->v = mxrealloc(j->v, (j->n + 1) * sizeof *j->v);
    memmove(&j->v[at + 1], &j->v[at], (j->n - at) * sizeof *j->v);
    j->v[at] = id;
    j->n++;
}

static inline void fjobs_del(fjobs *j, mj_job_id id) {
    for (size_t i = 0; i < j->n; i++)
        if (j->v[i] == id) {
            memmove(&j->v[i], &j->v[i + 1], (j->n - i - 1) * sizeof *j->v);
            j->n--;
            return;
        }
}

static inline void fjobs_free_v(void *v) { free(((fjobs *)v)->v); }
static inline void fanswer_free(void *v) { free(((fanswer *)v)->text); }
static inline void fpay_free(void *v) { free(((fpay *)v)->b); }

static inline mj_hash fs_sha(const void *b, size_t n) {
    mj_hash h;
    mesh_sha256(b, n, h.b);
    return h;
}

/* A simulated item's payload: the same bytes for the same item of every job, unless salted per job;
 * real pathtrace pass sums when the sim has them. A new buffer. */
static inline uint8_t *fs_payload_of(const fsim *s, mj_job_id job, uint64_t item, size_t *n) {
    if (s->real) {
        *n = s->real[item].n;
        uint8_t *b = mxalloc(*n);
        memcpy(b, s->real[item].b, *n);
        return b;
    }
    uint8_t *b = mxalloc(64);
    memset(b, 0x5a, 64);
    memcpy(b, &item, 8);
    if (s->salt) memcpy(b + 8, &job, 8);
    *n = 64;
    return b;
}

static inline fm fs_boot(fsim *s, uint32_t k, const msb *disk, const mfp_persist *fdisk) {
    uint64_t seed = fs_rand(s, (uint64_t)1 << 30) + 1;
    fm m = {0};
    if (s->n > 1) m.core = mfc_new((mfc_config){k, s->n, 3000, seed}, mfp_clone(fdisk), s->now);
    m.fdisk = mfp_clone(fdisk);
    mj_replica rep;
    if (!mj_replay(disk->p ? disk->p : "", disk->n, &rep, NULL, NULL, NULL)) abort();
    m.node = mj_node_new(fs_cfg(s, k), rep);
    msb_put(&m.disk, disk->p ? disk->p : "", disk->n);
    m.base = disk->n;
    m.blobs = mb_open(NULL, s->caps[k] ? s->caps[k] : (uint64_t)1 << 40, NULL);
    m.up = true;
    m.speed = 1.0;
    m.mem = momap_new(4, sizeof(fjobs), fjobs_free_v, NULL);
    m.told = momap_new(1, 1, NULL, NULL);
    return m;
}

static inline void fm_free(fm *m) {
    mfc_free(m->core);
    mfp_free(&m->fdisk);
    mj_node_free(m->node);
    for (size_t i = 0; i < m->nq; i++) mj_ranges_free(&m->q[i].items);
    free(m->q);
    msb_free(&m->disk);
    free(m->got);
    mb_store_free(m->blobs);
    momap_free(m->mem);
    momap_free(m->told);
    if (m->chk_valid) mj_replica_free(&m->chk);
    memset(m, 0, sizeof *m);
}

static inline void fs_relink(fsim *s);

static inline void fs_init(fsim *s, uint32_t n, uint64_t seed) {
    memset(s, 0, sizeof *s);
    s->n = n;
    s->now = 1;
    s->unix_ms = 1700000000000u;
    s->rng = seed | 1;
    s->answers = momap_new(1, sizeof(fanswer), fanswer_free, NULL);
    s->seen = momap_new(2, sizeof(uint32_t), NULL, NULL);
    s->bytes = momap_new(4, sizeof(fpay), fpay_free, NULL);
    s->min_copies = UINT32_MAX;
    for (uint32_t k = 0; k < NMAX; k++) s->caps[k] = (uint64_t)1 << 40;
    msb empty = {0};
    mfp_persist fresh = {0};
    for (uint32_t k = 0; k < n; k++) s->m[k] = fs_boot(s, k, &empty, &fresh);
    fs_relink(s);
}

static inline void fs_free(fsim *s) {
    for (uint32_t k = 0; k < s->n; k++) fm_free(&s->m[k]);
    for (size_t i = 0; i < s->nw; i++) {
        if (s->w[i].kind == FW_F) mfc_msg_free(&s->w[i].msg);
        mj_frame_free(&s->w[i].f);
    }
    free(s->w);
    momap_free(s->answers);
    momap_free(s->seen);
    for (size_t i = 0; i < s->nlogs; i++) free(s->logs[i]);
    free(s->logs);
    momap_free(s->bytes);
    for (size_t i = 0; i < s->nreal; i++) free(s->real[i].b);
    free(s->real);
}

static inline bool fs_conn(const fsim *s, uint32_t a, uint32_t b) {
    return s->m[a].up && s->m[b].up && !s->m[a].mute && !s->m[b].mute;
}

static inline void fs_put(fsim *s, uint32_t from, uint32_t to, fw w) {
    if (!fs_conn(s, from, to) || (s->drop > 0 && w.kind == FW_J && fs_rand(s, 1000) < s->drop)) {
        if (!fs_conn(s, from, to)) {
            /* not delivered */
        } else s->dropped++;
        if (w.kind == FW_F) mfc_msg_free(&w.msg);
        mj_frame_free(&w.f);
        return;
    }
    uint64_t last = s->last[from][to];
    uint64_t at = s->now + 1 + fs_rand(s, 4);
    if (last > at) at = last;
    s->last[from][to] = at;
    w.at = at;
    w.from = from;
    w.to = to;
    if (s->nw == s->capw) {
        s->capw = s->capw ? 2 * s->capw : 1024;
        s->w = mxrealloc(s->w, s->capw * sizeof *s->w);
    }
    s->w[s->nw++] = w;
}

static inline void fs_fleet_out(fsim *s, uint32_t k, mfc_outs o) {
    for (size_t i = 0; i < o.n; i++) {
        mfc_out *x = &o.v[i];
        if (x->kind == MFC_OUT_PERSIST) {
            mfp_free(&s->m[k].fdisk);
            s->m[k].fdisk = mfp_clone(mfc_persist(s->m[k].core));
        } else if (x->kind == MFC_OUT_SEND) {
            fs_put(s, k, x->to, (fw){.kind = FW_F, .msg = x->msg});
            x->msg = (mfc_msg){0};
        }
    }
    mfc_outs_free(&o);
}

static inline void fs_relink(fsim *s) {
    for (uint32_t a = 0; a < s->n; a++)
        for (uint32_t b = 0; b < s->n; b++) {
            if (a == b || !s->m[a].up || !s->m[a].core) continue;
            bool up = fs_conn(s, a, b);
            if (((mfc_linked(s->m[a].core) >> b & 1) != 0) != up) fs_fleet_out(s, a, mfc_link(s->m[a].core, b, up, s->now));
        }
}

static inline mj_view fs_view(const fsim *s, uint32_t k) {
    const fm *m = &s->m[k];
    if (!m->core) return (mj_view){1, true, k, (uint32_t)1 << k, (uint32_t)1 << k};
    uint32_t l;
    bool has = mfc_leader(m->core, &l);
    return (mj_view){mfc_term(m->core), has, l, mfc_alive(m->core, s->now), mfc_linked(m->core) | (uint32_t)1 << k};
}

static inline void fs_job_out(fsim *s, uint32_t k, mj_outs outs);

static inline bool fs_has_bytes(const fsim *s, uint32_t k, const mj_hash *h) {
    uint64_t w[4];
    mj_hash_words(h, w);
    return mb_has(s->m[k].blobs, h) || momap_get(s->m[k].mem, w) != NULL;
}

static inline uint32_t fs_held_by(const fsim *s, mj_job_id job, const mj_hash *h) {
    uint32_t m = 0;
    for (uint32_t k = 0; k < s->n; k++)
        if (mb_has_ref(s->m[k].blobs, job, h)) m |= (uint32_t)1 << k;
    return m;
}

typedef struct {
    const mj_table *t;
} fs_live_ctx;

static inline mb_live fs_live(void *ctx, mj_job_id j) { return mj_table_forgot(((fs_live_ctx *)ctx)->t, j) ? MB_FORGOTTEN : MB_LISTED; }

/* blobd's `ensure`: job's copy of blob h made durable in member k's store (a reference; room only by
 * evicting forgotten jobs'), and the node told; false: refused (full or forgotten). */
static inline bool fs_store(fsim *s, uint32_t k, mj_job_id job, mj_hash h) {
    if (!s->m[k].up) return false;
    fm *m = &s->m[k];
    if (!mb_has_ref(m->blobs, job, &h)) {
        const mj_table *t = &m->node->rep.committed;
        if (mj_table_forgot(t, job)) return false;
        uint64_t w[4];
        mj_hash_words(&h, w);
        const fpay *b = momap_get(s->bytes, w);
        fs_live_ctx cx = {t};
        mb_reserved r;
        if (!mb_reserve(m->blobs, job, h, (uint32_t)b->n, fs_live, &cx, &r, NULL)) {
            m->refused++;
            return false;
        }
        mb_put_mem(m->blobs, job, h, b->b, b->n);
        for (size_t i = 0; i < r.evicted.n; i++) mj_node_blob_gone(m->node, r.evicted.v[i].job, &r.evicted.v[i].h);
        mb_reserved_free(&r);
    }
    mj_outs o = mj_node_blob_stored(s->m[k].node, job, h, s->now, s->unix_ms);
    fs_job_out(s, k, o);
    return true;
}

/* blobd's news: forgotten jobs' references dropped (each told once a process). */
static inline void fs_news(fsim *s, uint32_t k) {
    fm *m = &s->m[k];
    const mj_table *t = &m->node->rep.committed;
    for (size_t i = 0; i < t->nforgotten; i++) {
        mj_job_id id = t->forgotten[i];
        bool fresh;
        momap_put1(m->told, id, &fresh);
        if (!fresh) continue;
        mb_refs g = mb_drop_job(m->blobs, id);
        for (size_t j = 0; j < g.n; j++) mj_node_blob_gone(m->node, id, &g.v[j].h);
        mb_refs_free(&g);
    }
}

static inline void fs_log(fsim *s, const char *line) {
    if (s->nlogs == s->caplogs) {
        s->caplogs = s->caplogs ? 2 * s->caplogs : 256;
        s->logs = mxrealloc(s->logs, s->caplogs * sizeof *s->logs);
    }
    s->logs[s->nlogs++] = mstrdup(line);
}

static inline bool fs_logged(const fsim *s, const char *a, const char *b) {
    for (size_t i = 0; i < s->nlogs; i++)
        if (strstr(s->logs[i], a) && (!b || strstr(s->logs[i], b))) return true;
    return false;
}

static inline void fs_crash(fsim *s, uint32_t k);

typedef struct {
    msb *buf;
    bool bad;
    bool *crash;
    bool *has_item;
    uint64_t item;
} fdisk;

static inline bool fd_append(void *c, const mj_log *l, char **err) {
    fdisk *d = c;
    if (d->bad) {
        *err = mstrdup("injected: no space left on device");
        return false;
    }
    mj_log_encode(l, d->buf);
    if (*d->crash) {
        *d->crash = false;
        *err = mstrdup("injected: crashed after the append");
        return false;
    }
    if (*d->has_item && l->kind == MJ_LOG_ENTRY)
        for (size_t i = 0; i < l->ops.n; i++)
            if (l->ops.v[i].kind == MJ_OP_DONE && l->ops.v[i].item == d->item) {
                *d->has_item = false;
                *err = mstrdup("injected: crashed after the append");
                return false;
            }
    return true;
}

static inline bool fd_sync(void *c, char **err) {
    (void)c;
    (void)err;
    return true;
}

static inline void fs_job_out(fsim *s, uint32_t k, mj_outs outs) {
    if (!s->m[k].up) {
        mj_outs_free(&outs);
        return;
    }
    fm *m = &s->m[k];
    fdisk d = {&m->disk, m->bad_disk, &m->crash_after_persist, &m->has_crash_item, m->crash_item};
    mj_disk dk = {&d, fd_append, fd_sync};
    bool ok;
    char *err = NULL;
    mj_outs v = mj_durable(&outs, &dk, &ok, &err);
    free(err);
    if (!ok) {
        mj_outs_free(&v);
        s->fail_stops++;
        fs_crash(s, k);
        return;
    }
    for (size_t i = 0; i < v.n; i++) {
        mj_out *o = &v.v[i];
        switch (o->kind) {
        case MJ_OUT_SEND:
            fs_put(s, k, o->to, (fw){.kind = FW_J, .f = o->frame});
            o->frame = (mj_frame){0};
            break;
        case MJ_OUT_RUN: {
            fm *mm = &s->m[k];
            mm->got = mxrealloc(mm->got, (mm->ngot + 1) * sizeof *mm->got);
            mm->got[mm->ngot++] = (fgot){o->run.lease, mj_ranges_len(&o->run.items)};
            if (mm->nq == mm->capq) {
                mm->capq = mm->capq ? 2 * mm->capq : 16;
                mm->q = mxrealloc(mm->q, mm->capq * sizeof *mm->q);
            }
            mm->q[mm->nq++] = (fqe){o->run.lease, o->run.job, o->run.items};
            o->run.items = (mj_ranges){0};
            break;
        }
        case MJ_OUT_CUT:
            for (size_t q = 0; q < s->m[k].nq; q++)
                if (s->m[k].q[q].lease == o->lease) mj_ranges_remove(&s->m[k].q[q].items, o->cut, UINT64_MAX);
            break;
        case MJ_OUT_ANSWER: {
            fanswer *a = momap_put1(s->answers, o->token, NULL);
            free(a->text);
            *a = (fanswer){o->ok, o->text};
            o->text = NULL;
            break;
        }
        case MJ_OUT_LOG: fs_log(s, o->text); break;
        case MJ_OUT_PUSH_BLOB:
            if (fs_has_bytes(s, k, &o->hash)) fs_put(s, k, o->to, (fw){.kind = FW_PUT, .job = o->job, .h = o->hash});
            break;
        case MJ_OUT_FETCH_BLOB: {
            if (fs_has_bytes(s, k, &o->hash)) {
                fs_store(s, k, o->job, o->hash);
                break;
            }
            int from = -1;
            for (uint32_t j = 0; j < s->n && from < 0; j++)
                if ((o->holders >> j & 1) && j != k && fs_conn(s, k, j) && fs_has_bytes(s, j, &o->hash)) from = (int)j;
            for (uint32_t j = 0; j < s->n && from < 0; j++)
                if (j != k && fs_conn(s, k, j) && fs_has_bytes(s, j, &o->hash)) from = (int)j;
            if (from >= 0) fs_put(s, (uint32_t)from, k, (fw){.kind = FW_GIVE, .job = o->job, .h = o->hash});
            break;
        }
        case MJ_OUT_BLOB_DONE: {
            uint64_t w[4];
            mj_hash_words(&o->hash, w);
            fjobs *js = momap_get(s->m[k].mem, w);
            if (js) {
                fjobs_del(js, o->job);
                if (!js->n) momap_del(s->m[k].mem, w);
            }
            break;
        }
        default: break;
        }
    }
    mj_outs_free(&v);
}

static inline void fs_crash(fsim *s, uint32_t k) {
    fm *m = &s->m[k];
    m->up = false;
    for (size_t i = 0; i < m->nq; i++) mj_ranges_free(&m->q[i].items);
    m->nq = 0;
    momap_clear(m->mem);
    fs_relink(s);
}

static inline size_t fs_up(const fsim *s) {
    size_t n = 0;
    for (uint32_t k = 0; k < s->n; k++) n += s->m[k].up;
    return n;
}

static inline void fs_restart(fsim *s, uint32_t k) {
    msb disk = {0};
    msb_put(&disk, s->m[k].disk.p ? s->m[k].disk.p : "", s->m[k].disk.n);
    mfp_persist fd = mfp_clone(&s->m[k].fdisk);
    double speed = s->m[k].speed;
    fm m = fs_boot(s, k, &disk, &fd);
    m.speed = speed;
    /* The store survives (its files); blobd's memory does not. */
    mb_store *keep = s->m[k].blobs;
    s->m[k].blobs = m.blobs;
    m.blobs = keep;
    fm_free(&s->m[k]);
    s->m[k] = m;
    msb_free(&disk);
    mfp_free(&fd);
    /* blobd's start: the store's references to the node; then the forgotten jobs' go (news). */
    mb_refs all = mb_all(s->m[k].blobs);
    for (size_t i = 0; i < all.n; i++) fs_store(s, k, all.v[i].job, all.v[i].h);
    mb_refs_free(&all);
    fs_news(s, k);
    fs_relink(s);
}

/* Jobs forgotten in some member's committed table. */
static inline bool fs_forgotten_any(const fsim *s, mj_job_id id) {
    for (uint32_t k = 0; k < s->n; k++)
        if (mj_table_forgot(&s->m[k].node->rep.committed, id)) return true;
    return false;
}

static inline unsigned fs_ones(uint32_t x) {
    unsigned c = 0;
    for (; x; x &= x - 1) c++;
    return c;
}

/* What member k shows committed must agree with everything shown before; its log replays to its state. */
static inline void fs_record(fsim *s, uint32_t k) {
    fm *m = &s->m[k];
    /* The log replays to the node's state: the replay is incremental (the records appended since the last
     * check applied to what the log before them replayed to, which is what a whole replay computes),
     * and whole after the log was rewritten. */
    bool ok = true;
    if (!m->chk_valid || m->disk.n < m->chk_at) {
        if (m->chk_valid) mj_replica_free(&m->chk);
        ok = mj_replay(m->disk.p ? m->disk.p : "", m->disk.n, &m->chk, NULL, &m->chk_at, NULL);
        m->chk_valid = ok;
    } else {
        const uint8_t *b = (const uint8_t *)m->disk.p;
        while (ok && m->disk.n - m->chk_at >= 9) {
            uint64_t len = 0;
            for (int i = 8; i >= 1; i--) len = len << 8 | b[m->chk_at + (size_t)i];
            if (m->disk.n - m->chk_at - 9 < len) break;
            mj_log l;
            ok = mj_log_decode(b[m->chk_at], b + m->chk_at + 9, (size_t)len, &l, NULL);
            if (!ok) break;
            mj_replica_apply(&m->chk, &l);
            mj_log_free(&l);
            m->chk_at += 9 + (size_t)len;
        }
    }
    if (!CHECK(ok && mj_replica_eq(&m->chk, &m->node->rep))) fprintf(stderr, "member %u: its log does not replay to its state\n", k);
    /* jobd's compaction (Store::wants_compact, scaled down): the log rewritten as the replica's. */
    if (m->disk.n > 2 * m->base + (64 << 10)) {
        mj_logs lg = mj_replica_log(&m->node->rep);
        msb_clear(&m->disk);
        for (size_t i = 0; i < lg.n; i++) mj_log_encode(&lg.v[i], &m->disk);
        mj_logs_free(&lg);
        m->base = m->disk.n;
        if (m->chk_valid) mj_replica_free(&m->chk);
        m->chk_valid = false;
    }
    if (m->node->rep.npending > s->max_pending) s->max_pending = m->node->rep.npending;
    const mj_table *t = &m->node->rep.committed;
    uint64_t id;
    for (const mj_job *j = mj_table_first(t, &id); j; j = mj_table_next(t, &id)) {
        bool forgotten = fs_forgotten_any(s, id);
        uint64_t item;
        momap_iter cur17_;
        momap_iter_init(&cur17_, j->results);
        for (const mj_res *res = momap_iter_next1(&cur17_, &item); res; res = momap_iter_next1(&cur17_, &item)) {
            uint64_t key[2] = {id, item};
            bool fresh;
            uint32_t *v = momap_put(s->seen, key, &fresh);
            if (fresh) {
                *v = res->result;
                if (res->has_blob) {
                    unsigned c = fs_ones(fs_held_by(s, id, &res->blob.hash));
                    if (c < s->min_copies) s->min_copies = c;
                }
            } else if (!CHECK_U(*v, res->result)) fprintf(stderr, "member %u: job %llx item %llu committed twice, differently\n", k, (unsigned long long)id, (unsigned long long)item);
            /* Every holder recorded holds the job's own copy, until the job is forgotten. */
            if (res->has_blob && !forgotten) {
                uint32_t held = fs_held_by(s, id, &res->blob.hash);
                if (!CHECK_U(res->blob.holders & ~held, 0))
                    fprintf(stderr, "member %u: job %llx item %llu: holders %#x, held by %#x\n", k, (unsigned long long)id, (unsigned long long)item,
                            res->blob.holders, held);
            }
        }
    }
}

static inline int fw_cmp(const void *a, const void *b) {
    const fw *x = a, *y = b;
    return x->at < y->at ? -1 : x->at > y->at ? 1 : 0;
}

static inline void fs_step(fsim *s, uint64_t ms) {
    uint64_t end = s->now + ms;
    while (s->now < end) {
        s->now++;
        s->unix_ms++;
        fw *due = NULL;
        size_t nd = 0, keep = 0;
        for (size_t i = 0; i < s->nw; i++) {
            if (s->w[i].at <= s->now) {
                due = mxrealloc(due, (nd + 1) * sizeof *due);
                due[nd] = s->w[i];
                due[nd].at = s->w[i].at * 1000000 + nd; /* stable */
                nd++;
            } else s->w[keep++] = s->w[i];
        }
        s->nw = keep;
        if (nd) qsort(due, nd, sizeof *due, fw_cmp);
        for (size_t i = 0; i < nd; i++) {
            fw *w = &due[i];
            if (!fs_conn(s, w->from, w->to)) {
                if (w->kind == FW_F) mfc_msg_free(&w->msg);
                mj_frame_free(&w->f);
                continue;
            }
            switch (w->kind) {
            case FW_F: fs_fleet_out(s, w->to, mfc_recv(s->m[w->to].core, w->from, w->msg, s->now)); break;
            case FW_J: fs_job_out(s, w->to, mj_node_recv(s->m[w->to].node, w->from, w->f, s->now, s->unix_ms)); break;
            case FW_PUT: {
                bool ok = fs_store(s, w->to, w->job, w->h);
                fs_put(s, w->to, w->from, (fw){.kind = FW_ACK, .job = w->job, .h = w->h, .ok = ok});
                break;
            }
            case FW_ACK: mj_node_blob_acked(s->m[w->to].node, w->from, w->job, w->h, w->ok); break;
            case FW_GIVE: fs_store(s, w->to, w->job, w->h); break;
            }
        }
        free(due);
        /* The executors. */
        for (uint32_t k = 0; k < s->n; k++) {
            fm *m = &s->m[k];
            if (!m->up || m->mute) continue;
            m->acc += m->speed;
            mj_item_res *res = NULL;
            size_t nres = 0;
            while (m->acc >= 1.0) {
                if (!m->nq) {
                    m->acc = 0.0;
                    break;
                }
                fqe *q = &m->q[0];
                uint64_t x;
                if (!mj_ranges_first(&q->items, &x)) {
                    mj_ranges_free(&q->items);
                    memmove(m->q, m->q + 1, (m->nq - 1) * sizeof *m->q);
                    m->nq--;
                    continue;
                }
                mj_ranges_take(&q->items, x);
                m->acc -= 1.0;
                m->ran++;
                bool fail = m->has_poison && m->poison == x;
                mj_item_res r = {.lease = q->lease, .job = q->job, .item = x, .status = fail ? MESH_JOBS_RS_FAIL : MESH_JOBS_RS_OK,
                                 .result = fail ? 0 : (uint32_t)(x * 7 + 1)};
                if (fail) {
                    r.out = (uint8_t *)mstrdup("poison");
                    r.out_len = 6;
                }
                if (s->payload && !fail) {
                    size_t n;
                    uint8_t *b = fs_payload_of(s, q->job, x, &n);
                    mj_hash h = fs_sha(b, n);
                    uint64_t w[4];
                    mj_hash_words(&h, w);
                    bool fresh;
                    fpay *p = momap_put(s->bytes, w, &fresh);
                    if (fresh) *p = (fpay){b, n, 0};
                    else free(b);
                    r.has_blob = true;
                    r.blob = (mj_blob_ref){h, (uint32_t)n, 0};
                }
                res = mxrealloc(res, (nres + 1) * sizeof *res);
                res[nres++] = r;
            }
            for (size_t i = 0; i < nres; i++) {
                mj_job_id job = res[i].job;
                bool hb = res[i].has_blob;
                mj_hash h = res[i].blob.hash;
                if (hb) {
                    uint64_t w[4];
                    mj_hash_words(&h, w);
                    fjobs_add(momap_put(s->m[k].mem, w, NULL), job);
                }
                fs_job_out(s, k, mj_node_result(s->m[k].node, res[i], s->now));
                if (hb && !fs_store(s, k, job, h) && s->m[k].up) mj_node_blob_acked(s->m[k].node, k, job, h, false);
            }
            free(res);
        }
        if (s->now % 5 == 0)
            for (uint32_t k = 0; k < s->n; k++) {
                if (!s->m[k].up || s->m[k].mute) continue;
                if (s->m[k].core) {
                    mfc_outs o = mfc_tick(s->m[k].core, s->now);
                    if (s->now % 250 == 0) {
                        for (uint32_t j = 0; j < s->n; j++) {
                            if (j == k) continue;
                            if (o.n == o.cap) {
                                o.cap = o.cap ? 2 * o.cap : 8;
                                o.v = mxrealloc(o.v, o.cap * sizeof *o.v);
                            }
                            o.v[o.n++] = (mfc_out){.kind = MFC_OUT_SEND, .to = j, .msg = mfc_beat(s->m[k].core)};
                        }
                    }
                    fs_fleet_out(s, k, o);
                }
                fs_job_out(s, k, mj_node_set_view(s->m[k].node, fs_view(s, k), s->now, s->unix_ms));
                fs_job_out(s, k, mj_node_tick(s->m[k].node, s->now, s->unix_ms));
                if (s->now % 250 == 0) fs_news(s, k);
                if (s->now % 200 == 0) fs_record(s, k);
            }
    }
}

static inline int fs_leader(const fsim *s) {
    for (uint32_t k = 0; k < s->n; k++)
        if (s->m[k].up && !s->m[k].mute && mj_node_active(s->m[k].node)) return (int)k;
    return -1;
}

static inline void fs_progs(fsim *s, const uint32_t *ks, size_t n, uint32_t cap) {
    for (size_t i = 0; i < n; i++) {
        mj_prog *p = mxalloc(sizeof *p);
        *p = (mj_prog){mstrdup("p"), 0, cap};
        mj_node_set_progs(s->m[ks[i]].node, p, 1);
    }
}

static inline void fs_set_progs(fsim *s, uint32_t k, const char *const *names, const uint32_t *exec, const uint32_t *cap, size_t n) {
    mj_prog *p = mxalloc(n * sizeof *p);
    for (size_t i = 0; i < n; i++) p[i] = (mj_prog){mstrdup(names[i]), exec[i], cap[i]};
    mj_node_set_progs(s->m[k].node, p, n);
}

static inline uint64_t fs_ask(fsim *s, uint32_t k, const char *words) {
    s->token++;
    uint64_t t = s->token;
    char *line = mstrdup(words);
    const char *w[64];
    size_t n = 0;
    for (char *tok = strtok(line, " "); tok && n < 64; tok = strtok(NULL, " ")) w[n++] = tok;
    mj_req r;
    char *err = NULL;
    if (!mj_req_parse(w, n, &r, &err)) {
        fprintf(stderr, "ask: %s\n", err);
        abort();
    }
    free(line);
    fs_job_out(s, k, mj_node_request(s->m[k].node, r, t, s->now, s->unix_ms));
    return t;
}

/* The answer to request t (its text; *ok whether it succeeded), stepping up to `within` ms. */
static inline char *fs_answer(fsim *s, uint64_t t, uint64_t within, bool *ok) {
    uint64_t t0 = s->now;
    while (!momap_get1(s->answers, t)) {
        fs_step(s, 5);
        if (s->now - t0 >= within) {
            fprintf(stderr, "no answer to request %llu\n", (unsigned long long)t);
            abort();
        }
    }
    fanswer a;
    momap_take(s->answers, &t, &a);
    *ok = a.ok;
    return a.text;
}

static inline mj_job_id fs_submit(fsim *s, uint32_t k, const char *words) {
    bool ok;
    char *a = fs_answer(s, fs_ask(s, k, words), 5000, &ok);
    if (!ok) {
        fprintf(stderr, "submit: %s\n", a);
        abort();
    }
    mj_job_id id = 0;
    char w[64];
    if (sscanf(a, "job %63s", w) != 1 || !mj_parse_job(w, &id)) abort();
    free(a);
    return id;
}

/* Until job id is finished in member k's committed table (a clone of it). */
static inline mj_job *fs_finish(fsim *s, uint32_t k, mj_job_id id, uint64_t within) {
    uint64_t t0 = s->now;
    for (;;) {
        const mj_job *j = mj_table_get(&s->m[k].node->rep.committed, id);
        if (j && j->finished) return mj_job_clone(j);
        fs_step(s, 10);
        if (s->now - t0 >= within) {
            j = mj_table_get(&s->m[k].node->rep.committed, id);
            fprintf(stderr, "job %llx not finished in %llu ms on member %u: done %llu failed %zu state %s\n", (unsigned long long)id,
                    (unsigned long long)within, k, j ? (unsigned long long)mj_ranges_len(&j->done) : 0ULL, j ? momap_len(j->failed) : 0,
                    j ? mj_job_state(j) : "-");
            abort();
        }
    }
}

/* Every item of the job exactly once, with its result. */
static inline void fs_exactly_once(const mj_job *j) {
    CHECK_U(momap_len(j->results), mj_job_total(j));
    for (size_t r = 0; r < j->spec.items.n; r++)
        for (uint64_t x = j->spec.items.v[r][0]; x < j->spec.items.v[r][1]; x++) {
            const mj_res *res = momap_get1(j->results, x);
            if (!CHECK(res && res->result == (uint32_t)(x * 7 + 1))) {
                fprintf(stderr, "item %llu\n", (unsigned long long)x);
                return;
            }
        }
    CHECK(mj_ranges_eq(&j->done, &j->spec.items));
}

/* The most items of job id any member up shows done. */
static inline uint64_t fs_done_of(const fsim *s, mj_job_id id) {
    uint64_t best = 0;
    for (uint32_t k = 0; k < s->n; k++) {
        if (!s->m[k].up) continue;
        const mj_job *j = mj_table_get(&s->m[k].node->rep.committed, id);
        if (j && mj_ranges_len(&j->done) > best) best = mj_ranges_len(&j->done);
    }
    return best;
}

typedef bool (*fs_pred)(const fsim *s, const void *cx);

static inline void fs_until(fsim *s, const char *what, uint64_t within, fs_pred f, const void *cx) {
    uint64_t t0 = s->now;
    while (!f(s, cx)) {
        fs_step(s, 10);
        if (s->now - t0 >= within) {
            fprintf(stderr, "%s: not within %llu ms\n", what, (unsigned long long)within);
            for (uint32_t k = 0; k < s->n; k++) {
                const fm *m = &s->m[k];
                mj_view v = fs_view(s, k);
                fprintf(stderr, "  m%u up %d leader %d staged %zu parked %zu refused %llu bytes %llu pending %zu\n", k, m->up, v.has_leader ? (int)v.leader : -1,
                        mj_node_staged(m->node), mj_node_parked(m->node), (unsigned long long)m->refused, (unsigned long long)mb_bytes(m->blobs),
                        m->node->rep.npending);
            }
            abort();
        }
    }
}

static inline bool fs_has_leader(const fsim *s, const void *cx) {
    (void)cx;
    return fs_leader(s) >= 0;
}

/* blobd's assembly of job id on member k: every committed result's blob read here or fetched from a
 * member up that has it, checked, folded in pass order. NULL (with *err) if a blob is held by no
 * member up. */
static inline masm *fs_assemble(fsim *s, uint32_t k, mj_job_id id, const mj_setup *setup, size_t nsetup, char **err) {
    const mj_job *j0 = mj_table_get(&s->m[k].node->rep.committed, id);
    if (!j0) {
        *err = mstrdup("not in the table");
        return NULL;
    }
    mj_job *j = mj_job_clone(j0);
    masm *a = masm_new(id, setup, nsetup, 0, err);
    uint64_t item;
    for (const mj_res *r = momap_first(j->results, &item); a && r; r = momap_next(j->results, &item, &item)) {
        if (!r->has_blob) {
            *err = mstrdup("a result without a blob");
            masm_free(a);
            a = NULL;
            break;
        }
        masm_commit(a, item, (masm_item){r->blob.hash, r->blob.len, r->blob.holders, r->member, s->real[item].digest});
        int from = -1;
        if (mb_has(s->m[k].blobs, &r->blob.hash)) from = (int)k;
        for (uint32_t x = 0; x < s->n && from < 0; x++)
            if (x != k && fs_conn(s, k, x) && mb_has(s->m[x].blobs, &r->blob.hash)) from = (int)x;
        if (from < 0) {
            *err = mfmt("item %llu: its blob is held by no member up (holders %#x)", (unsigned long long)item, r->blob.holders);
            masm_free(a);
            a = NULL;
            break;
        }
        size_t n;
        uint8_t *b = mb_get(s->m[from].blobs, &r->blob.hash, &n);
        if (masm_blob(a, item, b, n, err) < 0) {
            free(b);
            masm_free(a);
            a = NULL;
            break;
        }
        free(b);
    }
    mj_job_free(j);
    return a;
}

/* A small pathtrace scene's setup (6 tiles, 64 passes of 1 spp: 384 items) and every item's real
 * payload (its pass sums) with its pass-sum digest, rendered on a few threads (each item independent). */
typedef struct {
    const uint32_t *sc;
    uint32_t tiles;
    fpay *pays;
    size_t from, to;
} fs_render_job;

static inline void *fs_render(void *arg) {
    fs_render_job *j = arg;
    pt_v *part = mxalloc(PT_TILE_PIX * sizeof *part);
    for (size_t w = j->from; w < j->to; w++) {
        memset(part, 0, PT_TILE_PIX * sizeof *part);
        uint64_t d = pt_tile_pass_z(j->sc, (uint32_t)(w % j->tiles), (uint32_t)(w / j->tiles), part);
        uint8_t *b = mxalloc(PT_PAYLOAD);
        for (size_t i = 0; i < PT_TILE_PIX; i++) {
            uint32_t ws[3] = {mesh_ftz_bits(part[i].x), mesh_ftz_bits(part[i].y), mesh_ftz_bits(part[i].z)};
            for (int c = 0; c < 3; c++)
                for (int k = 0; k < 4; k++) b[12 * i + 4 * (size_t)c + (size_t)k] = (uint8_t)(ws[c] >> 8 * k);
        }
        j->pays[w] = (fpay){b, PT_PAYLOAD, d};
    }
    free(part);
    return NULL;
}

static inline void fs_small_scene(mj_setup setup[5], fpay **pays, size_t *n) {
    setup[0] = (mj_setup){5, 0};
    setup[1] = (mj_setup){7, pt_set_arg(PT_H_WIDTH, 96)};
    setup[2] = (mj_setup){7, pt_set_arg(PT_H_HEIGHT, 48)};
    setup[3] = (mj_setup){7, pt_set_arg(PT_H_PASSES, 64)};
    setup[4] = (mj_setup){7, pt_set_arg(PT_H_SPP, 1)};
    static uint32_t sc[PT_DEFAULT_SCENE_WORDS];
    if (!masm_job_scene(setup, 5, sc, NULL)) abort();
    uint32_t tiles = pt_tiles(sc), passes = sc[PT_H_PASSES];
    *n = (size_t)tiles * passes;
    *pays = mxalloc(*n * sizeof **pays);
    enum { T = 2 };
    pthread_t th[T];
    fs_render_job jobs[T];
    for (size_t t = 0; t < T; t++) {
        jobs[t] = (fs_render_job){sc, tiles, *pays, *n * t / T, *n * (t + 1) / T};
        if (pthread_create(&th[t], NULL, fs_render, &jobs[t]) != 0) abort();
    }
    for (size_t t = 0; t < T; t++) pthread_join(th[t], NULL);
}

#endif
