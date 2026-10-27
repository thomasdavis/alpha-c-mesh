/* jobs.rs's Node: construction, the inputs (set_view, recv, result, blob_*, set_progs, request,
 * tick) and the member side (stage). The leader's side is in lead.c, frames in frames.c. */
#include "internal.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/jobs/copies.h"
#include "mesh/jobs/lid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *mj_word(mj_job_id id) {
    char w[MJ_JOB_WORD_MAX];
    mj_job_word(id, w);
    return mstrdup(w);
}

/* ---- (member, program) maps -------------------------------------------------------------------- */

static int mp_cmp(uint32_t m, const char *p, const mj_mp *x) {
    if (m != x->member) return m < x->member ? -1 : 1;
    return strcmp(p, x->program);
}

mj_mp *mj_mpmap_get(const mj_mpmap *m, uint32_t member, const char *program) {
    for (size_t i = 0; i < m->n; i++)
        if (mp_cmp(member, program, &m->v[i]) == 0) return &m->v[i];
    return NULL;
}

mj_mp *mj_mpmap_put(mj_mpmap *m, uint32_t member, const char *program) {
    size_t at = 0;
    while (at < m->n) {
        int c = mp_cmp(member, program, &m->v[at]);
        if (c == 0) return &m->v[at];
        if (c < 0) break;
        at++;
    }
    if (m->n == m->cap) {
        m->cap = m->cap ? 2 * m->cap : 8;
        m->v = mxrealloc(m->v, m->cap * sizeof *m->v);
    }
    memmove(&m->v[at + 1], &m->v[at], (m->n - at) * sizeof *m->v);
    m->v[at] = (mj_mp){.member = member, .program = mstrdup(program)};
    m->n++;
    return &m->v[at];
}

static void mpmap_free(mj_mpmap *m) {
    for (size_t i = 0; i < m->n; i++) free(m->v[i].program);
    free(m->v);
    *m = (mj_mpmap){0};
}

/* ---- map value releases ------------------------------------------------------------------------ */

static void lease_free(void *v) {
    mj_lease *x = v;
    free(x->program);
    mj_ranges_free(&x->out);
}
static void queue_free(void *v) {
    mj_queue *q = v;
    mj_ranges_free(&q->fresh);
    mj_ranges_free(&q->retry);
}
static void picks_free(void *v) { mj_picks_free(v); }
static void parked_free(void *v) {
    mj_parked_list *p = v;
    for (size_t i = 0; i < p->n; i++) mj_item_res_free(&p->v[i].r);
    free(p->v);
}
static void held_free(void *v) { mj_held_free(v); }
static void outbox_free(void *v) { mj_item_res_free(&((mj_outbox_entry *)v)->r); }
static void staged_free(void *v) { mj_item_res_free(&((mj_staged *)v)->r); }

mj_lead *mj_lead_new(uint32_t n);
void mj_lead_free(mj_lead *l);

mj_lead *mj_lead_new(uint32_t n) {
    mj_lead *l = mxalloc(sizeof *l);
    mj_table_init(&l->working);
    l->acked = mxalloc((n ? n : 1) * sizeof *l->acked);
    l->disp.leases = momap_new(1, sizeof(mj_lease), lease_free, NULL);
    l->disp.queues = momap_new(1, sizeof(mj_queue), queue_free, NULL);
    l->disp.picks = momap_new(1, sizeof(mj_picks), picks_free, NULL);
    l->dupes = momap_new(1, sizeof(uint64_t), NULL, NULL);
    l->parked = momap_new(5, sizeof(mj_parked_list), parked_free, NULL);
    l->fetch_at = momap_new(5, sizeof(uint64_t), NULL, NULL);
    return l;
}

void mj_lead_free(mj_lead *l) {
    if (!l) return;
    for (int k = 0; k < MJ_MEMBERS; k++)
        if (l->answers[k]) {
            mj_answer_free(l->answers[k]);
            free(l->answers[k]);
        }
    for (size_t i = 0; i < l->nadopt; i++) mj_held_free(&l->adopt[i].h);
    free(l->adopt);
    mj_table_free(&l->working);
    free(l->acked);
    mj_ops_free(&l->ops);
    for (size_t i = 0; i < l->nwaits; i++) free(l->waits[i].text);
    free(l->waits);
    momap_free(l->disp.leases);
    momap_free(l->disp.queues);
    mj_member_progs_free(&l->disp.progs);
    mpmap_free(&l->disp.rates);
    mpmap_free(&l->disp.holdoff);
    momap_free(l->disp.picks);
    momap_free(l->dupes);
    for (size_t i = 0; i < l->nheld; i++) mj_submit_free(&l->held[i].s);
    free(l->held);
    momap_free(l->parked);
    momap_free(l->fetch_at);
    free(l);
}

/* ---- the node ---------------------------------------------------------------------------------- */

mj_node *mj_node_new(mj_cfg cfg, mj_replica rep) {
    mj_node *n = mxalloc(sizeof *n);
    n->cfg = cfg;
    n->rep = rep;
    mj_table_init(&n->staging);
    n->held = momap_new(1, sizeof(mj_held), held_free, NULL);
    n->outbox = momap_new(3, sizeof(mj_outbox_entry), outbox_free, NULL);
    n->avail_dirty = true;
    n->staged = momap_new(3, sizeof(mj_staged), staged_free, NULL);
    n->have = momap_new(5, 1, NULL, NULL);
    return n;
}

static void sched_free(mj_sched *s) {
    if (!s) return;
    if (s->release) s->release(s->ctx);
    free(s);
}

void mj_node_free(mj_node *n) {
    if (!n) return;
    mj_replica_free(&n->rep);
    mj_table_free(&n->staging);
    mj_lead_free(n->lead);
    momap_free(n->held);
    momap_free(n->outbox);
    mj_progs_free(n->progs, n->nprogs);
    for (size_t i = n->lo_head; i < n->nlo; i++) mj_frame_free(&n->lo[i]);
    free(n->lo);
    sched_free(n->sched);
    momap_free(n->staged);
    momap_free(n->have);
    free(n);
}

const mj_table *mj_node_committed(const mj_node *n) { return &n->rep.committed; }

void mj_node_set_sched(mj_node *n, mj_sched *cx) {
    sched_free(n->sched);
    n->sched = cx;
}

const mj_picks *mj_node_picks(const mj_node *n, mj_job_id job) { return n->lead ? momap_get1(n->lead->disp.picks, job) : NULL; }
bool mj_node_leading(const mj_node *n) { return n->lead != NULL; }
bool mj_node_active(const mj_node *n) { return n->lead && n->lead->active; }
uint64_t mj_node_lid(const mj_node *n) { return n->rep.lid; }
uint64_t mj_node_commit_n(const mj_node *n) { return n->lead ? n->lead->commit : n->rep.commit_n; }
const mj_table *mj_node_working(const mj_node *n) { return n->lead && n->lead->active ? &n->lead->working : NULL; }
const momap *mj_node_leases(const mj_node *n) { return n->lead ? n->lead->disp.leases : NULL; }
const mj_mpmap *mj_node_rates(const mj_node *n) { return n->lead ? &n->lead->disp.rates : NULL; }

bool mj_node_queued(const mj_node *n, mj_job_id job, uint64_t *count) {
    if (!n->lead) return false;
    const mj_queue *q = momap_get1(n->lead->disp.queues, job);
    if (!q) return false;
    uint64_t a = mj_ranges_len(&q->fresh), b = mj_ranges_len(&q->retry);
    *count = a > UINT64_MAX - b ? UINT64_MAX : a + b;
    return true;
}

double mj_node_my_rate(const mj_node *n) { return n->rate.ips; }
size_t mj_node_unconfirmed(const mj_node *n) { return momap_len(n->outbox); }
size_t mj_node_staged(const mj_node *n) { return momap_len(n->staged); }

size_t mj_node_parked(const mj_node *n) {
    if (!n->lead) return 0;
    size_t total = 0;
    uint64_t k[5];
    momap_iter cur4_;
    momap_iter_init(&cur4_, n->lead->parked);
    const uint64_t *cur4_k;
    for (const mj_parked_list *p = momap_iter_next(&cur4_, &cur4_k); p && (memcpy(k, cur4_k, sizeof k), 1); p = momap_iter_next(&cur4_, &cur4_k)) total += p->n;
    return total;
}

bool mj_node_has_blob(const mj_node *n, mj_job_id job, const mj_hash *h) {
    uint64_t k[5];
    mj_key5(k, job, h);
    return momap_get(n->have, k) != NULL;
}

void mj_node_send(mj_node *n, uint32_t k, mj_frame f, mj_outs *out) {
    if (k == n->cfg.me) {
        if (n->nlo == n->caplo) {
            if (n->lo_head > 0) {
                memmove(n->lo, n->lo + n->lo_head, (n->nlo - n->lo_head) * sizeof *n->lo);
                n->nlo -= n->lo_head;
                n->lo_head = 0;
            }
            if (n->nlo == n->caplo) {
                n->caplo = n->caplo ? 2 * n->caplo : 16;
                n->lo = mxrealloc(n->lo, n->caplo * sizeof *n->lo);
            }
        }
        n->lo[n->nlo++] = f;
    } else mj_outs_push(out, mj_out_send(k, f));
}

void mj_node_log(mj_node *n, mj_log l, mj_outs *out) {
    mj_replica_apply(&n->rep, &l);
    mj_outs_push(out, mj_out_persist(l));
}

void mj_node_pump(mj_node *n, uint64_t now, uint64_t unix_ms, mj_outs *out) {
    while (n->lo_head < n->nlo) {
        mj_frame f = n->lo[n->lo_head++];
        if (n->lo_head == n->nlo) n->lo_head = n->nlo = 0;
        mj_node_on_frame(n, n->cfg.me, f, now, unix_ms, out);
    }
}

/* ---- inputs ------------------------------------------------------------------------------------ */

mj_outs mj_node_set_view(mj_node *n, mj_view v, uint64_t now, uint64_t unix_ms) {
    mj_outs out = {0};
    mj_view old = n->view;
    n->view = v;
    uint32_t me = n->cfg.me;
    bool lead_me = v.has_leader && v.leader == me;
    if (n->lead && (!lead_me || n->lead->term != v.term)) {
        char who[16];
        if (v.has_leader) snprintf(who, sizeof who, "%u", v.leader);
        else strcpy(who, "-");
        mj_outs_push(&out, mj_out_log(mfmt("jobs: system %u no longer leads the job table (fleet leader %s, term %llu)", me, who,
                                           (unsigned long long)v.term)));
        mj_outs o = mj_node_step_down(n);
        mj_outs_append(&out, &o);
    }
    if (lead_me && !n->lead) mj_node_start_lead(n, v.term, now, unix_ms, &out);
    if (n->lead) {
        mj_lead *l = n->lead;
        /* A member linked since: it gets a snapshot (once this leadership is active). */
        uint32_t fresh = v.linked & ~old.linked & ~((uint32_t)1 << me);
        l->snap_due |= fresh;
        /* Pull again from members newly linked while pulling. */
        if (!l->active)
            for (uint32_t k = 0; k < 32; k++)
                if (fresh >> k & 1) mj_outs_push(&out, mj_out_send(k, mj_frame_pull(l->lid)));
    }
    bool same_leader = v.has_leader == old.has_leader && (!v.has_leader || v.leader == old.leader);
    if (!same_leader) {
        n->avail_dirty = true;
        uint64_t k[3];
        momap_iter cur5_;
        momap_iter_init(&cur5_, n->outbox);
        const uint64_t *cur5_k;
        for (mj_outbox_entry *e = momap_iter_next(&cur5_, &cur5_k); e && (memcpy(k, cur5_k, sizeof k), 1); e = momap_iter_next(&cur5_, &cur5_k)) e->at = 0;
    }
    mj_node_pump(n, now, unix_ms, &out);
    return out;
}

mj_outs mj_node_recv(mj_node *n, uint32_t from, mj_frame f, uint64_t now, uint64_t unix_ms) {
    mj_outs out = {0};
    mj_node_on_frame(n, from, f, now, unix_ms, &out);
    mj_node_pump(n, now, unix_ms, &out);
    return out;
}

mj_outs mj_node_result(mj_node *n, mj_item_res r, uint64_t now) {
    mj_held *h = momap_get1(n->held, r.lease);
    if (h) {
        mj_ranges_take(&h->items, r.item);
        if (mj_ranges_is_empty(&h->items)) {
            momap_del1(n->held, r.lease);
            n->avail_dirty = true;
        }
    }
    if (r.status != MESH_JOBS_RS_RETURNED) mj_rate_count(&n->rate, 1, now);
    uint64_t key[3];
    mj_key3(key, r.job, r.item, r.lease);
    if (r.has_blob && r.status == MESH_JOBS_RS_OK) {
        uint32_t acked = mj_node_has_blob(n, r.job, &r.blob.hash) ? (uint32_t)1 << n->cfg.me : 0;
        momap_del(n->staged, key);
        mj_staged *s = momap_put(n->staged, key, NULL);
        s->r = r;
        s->acked = acked;
    } else {
        momap_del(n->outbox, key);
        mj_outbox_entry *e = momap_put(n->outbox, key, NULL);
        e->r = r;
        e->at = 0;
    }
    return (mj_outs){0};
}

static bool staged_for(const mj_staged *s, mj_job_id job, const mj_hash *h) {
    return s->r.job == job && s->r.has_blob && mj_hash_eq(&s->r.blob.hash, h);
}

mj_outs mj_node_blob_stored(mj_node *n, mj_job_id job, mj_hash hash, uint64_t now, uint64_t unix_ms) {
    mj_outs out = {0};
    uint64_t hk[5];
    mj_key5(hk, job, &hash);
    momap_put(n->have, hk, NULL);
    uint32_t me = n->cfg.me;
    uint64_t k[3];
    momap_iter cur6_;
    momap_iter_init(&cur6_, n->staged);
    const uint64_t *cur6_k;
    for (mj_staged *s = momap_iter_next(&cur6_, &cur6_k); s && (memcpy(k, cur6_k, sizeof k), 1); s = momap_iter_next(&cur6_, &cur6_k))
        if (staged_for(s, job, &hash)) s->acked |= (uint32_t)1 << me;
    if (n->lead && n->lead->active) {
        mj_lead *l = n->lead;
        momap_del(l->fetch_at, hk);
        mj_parked_list v = {0};
        if (momap_take(l->parked, hk, &v)) {
            /* Grouped by the member it came from, in member order, each in arrival order. */
            for (uint32_t from = 0; from < 32; from++) {
                size_t cnt = 0;
                for (size_t i = 0; i < v.n; i++) cnt += v.v[i].from == from;
                if (!cnt) continue;
                mj_item_res *items = mxalloc(cnt * sizeof *items);
                size_t j = 0;
                for (size_t i = 0; i < v.n; i++)
                    if (v.v[i].from == from) {
                        items[j++] = v.v[i].r;
                        v.v[i].r = (mj_item_res){0};
                    }
                mj_node_on_results(n, from, items, cnt, now, unix_ms, &out);
            }
            parked_free(&v);
        }
    }
    mj_node_pump(n, now, unix_ms, &out);
    return out;
}

void mj_node_blob_gone(mj_node *n, mj_job_id job, const mj_hash *hash) {
    uint64_t k[5];
    mj_key5(k, job, hash);
    momap_del(n->have, k);
}

void mj_node_blob_acked(mj_node *n, uint32_t from, mj_job_id job, mj_hash hash, bool ok) {
    uint64_t k[3];
    momap_iter cur7_;
    momap_iter_init(&cur7_, n->staged);
    const uint64_t *cur7_k;
    for (mj_staged *s = momap_iter_next(&cur7_, &cur7_k); s && (memcpy(k, cur7_k, sizeof k), 1); s = momap_iter_next(&cur7_, &cur7_k))
        if (staged_for(s, job, &hash)) {
            if (ok) s->acked |= (uint32_t)1 << from;
            else s->refused |= (uint32_t)1 << from;
        }
}

void mj_node_set_progs(mj_node *n, mj_prog *progs, size_t count) {
    if (mj_progs_eq(progs, count, n->progs, n->nprogs)) {
        mj_progs_free(progs, count);
        return;
    }
    mj_progs_free(n->progs, n->nprogs);
    n->progs = progs;
    n->nprogs = count;
    n->avail_dirty = true;
}

/* Members (alive & allowed) whose programs include one of the submit's. */
static uint32_t runs_mask(const mj_lead *l, const mj_submit *s, uint32_t alive) {
    uint32_t allowed = s->members == 0 ? UINT32_MAX : s->members, mask = 0;
    mj_spec probe = {.program = s->program};
    for (uint32_t k = 0; k < MJ_MEMBERS; k++) {
        if (!l->disp.progs.has[k] || ((alive & allowed) >> k & 1) == 0) continue;
        for (size_t i = 0; i < l->disp.progs.n[k]; i++)
            if (mj_spec_runs(&probe, l->disp.progs.v[k][i].name)) {
                mask |= (uint32_t)1 << k;
                break;
            }
    }
    return mask;
}

static void push_wait(mj_lead *l, uint64_t n, uint64_t token, bool ok, char *text) {
    if (l->nwaits == l->capwaits) {
        l->capwaits = l->capwaits ? 2 * l->capwaits : 8;
        l->waits = mxrealloc(l->waits, l->capwaits * sizeof *l->waits);
    }
    l->waits[l->nwaits++] = (mj_wait){n, token, ok, text};
}

static mj_outs request_in(mj_node *n, mj_req req, uint64_t token, uint64_t now, uint64_t unix_ms, bool may_wait) {
    mj_outs out = {0};
    uint32_t me = n->cfg.me;
    uint32_t alive = n->view.alive | (uint32_t)1 << me;
    mj_lead *l = n->lead && n->lead->active ? n->lead : NULL;
    if (!l) {
        mj_outs_push(&out, mj_out_answer(token, false,
                                         mstrdup(n->lead ? "jobs: the new leader is still merging the members' job tables; try again in a moment"
                                                         : "jobs: this member does not lead the job table")));
        mj_req_free(&req);
        return out;
    }
    bool ok = false;
    char *text = NULL;
    switch (req.kind) {
    case MJ_REQ_SUBMIT: {
        mj_submit *s = &req.submit;
        uint32_t runs = runs_mask(l, s, alive);
        if (!runs && may_wait) {
            /* Held until a member reports the program (or SUBMIT_WAIT_MS): tick retries it. */
            if (l->nheld == l->capheld) {
                l->capheld = l->capheld ? 2 * l->capheld : 4;
                l->held = mxrealloc(l->held, l->capheld * sizeof *l->held);
            }
            l->held[l->nheld++] = (mj_held_submit){*s, token, now + MESH_JOBS_SUBMIT_WAIT_MS};
            return out;
        }
        if (!runs) {
            text = mfmt("no member runs %s (a loaded, running program with policy \"job\") after %u s; `jobs` shows who runs what", s->program,
                        MESH_JOBS_SUBMIT_WAIT_MS / 1000);
            break;
        }
        mj_job_id id = mj_table_next_id(&l->working, l->term);
        mj_spec spec = {.id = id, .program = mstrdup(s->program), .op = s->op, .items = mj_ranges_clone(&s->items), .priority = s->priority,
                        .attempts = s->attempts, .deadline_ms = s->deadline_s > 0 ? unix_ms + s->deadline_s * 1000 : 0, .mode = s->mode,
                        .setup = mj_setup_clone(s->setup, s->nsetup), .nsetup = s->nsetup, .submitted_ms = unix_ms, .members = s->members};
        mj_op op = mj_op_submit(&spec);
        mj_table_apply(&l->working, &op);
        mj_ops_push(&l->ops, op);
        bool fresh;
        mj_queue *q = momap_put1(l->disp.queues, id, &fresh);
        if (!fresh) queue_free(q);
        *q = (mj_queue){mj_ranges_clone(&s->items), {0}};
        msb b = {0};
        char *w = mj_word(id);
        msb_printf(&b, "job %s program %s items ", w, s->program);
        free(w);
        if (mj_spec_endless(&spec)) msb_puts(&b, "endless");
        else msb_printf(&b, "%llu", (unsigned long long)mj_ranges_len(&s->items));
        uint32_t cnt = 0;
        for (uint32_t k = 0; k < 32; k++) cnt += runs >> k & 1;
        msb_printf(&b, " on %u member(s) (", cnt);
        bool first = true;
        for (uint32_t k = 0; k < 32; k++)
            if (runs >> k & 1) {
                msb_printf(&b, "%s%u", first ? "" : ",", k);
                first = false;
            }
        msb_putc(&b, ')');
        mj_spec_free(&spec);
        ok = true;
        text = msb_take(&b);
        break;
    }
    case MJ_REQ_CANCEL: {
        mj_job *j = mj_table_get(&l->working, req.job);
        char *w = mj_word(req.job);
        if (!j) text = mfmt("no job %s", w);
        else if (!mj_job_active(j)) text = mfmt("job %s is %s already", w, mj_job_state(j));
        else {
            mj_lead_cancel(l, req.job, "cancelled", MESH_JOBS_FIN_CANCELLED, unix_ms, &out);
            ok = true;
            text = mfmt("job %s cancelled", w);
        }
        free(w);
        break;
    }
    case MJ_REQ_TONE: {
        mj_job *j = mj_table_get(&l->working, req.job);
        char *w = mj_word(req.job);
        if (!j) text = mfmt("no job %s", w);
        else {
            mj_op op = mj_op_tone(req.job, j->tone_seq + 1, req.tone);
            mj_table_apply(&l->working, &op);
            mj_ops_push(&l->ops, op);
            ok = true;
            text = mfmt("job %s tone %u", w, req.tone);
        }
        free(w);
        break;
    }
    case MJ_REQ_FORGET: {
        mj_job *j = mj_table_get(&l->working, req.job);
        char *w = mj_word(req.job);
        if (!j) text = mfmt("no job %s", w);
        else if (!j->finished) text = mfmt("job %s is %s: cancel it first", w, mj_job_state(j));
        else {
            mj_op op = mj_op_forget(req.job);
            mj_table_apply(&l->working, &op);
            mj_ops_push(&l->ops, op);
            momap_del1(l->disp.queues, req.job);
            uint64_t k[5];
            for (mj_parked_list *p = momap_first(l->parked, k); p; p = momap_next(l->parked, k, k)) {
                size_t keep = 0;
                for (size_t i = 0; i < p->n; i++) {
                    if (p->v[i].r.job != req.job) p->v[keep++] = p->v[i];
                    else mj_item_res_free(&p->v[i].r);
                }
                p->n = keep;
                if (!keep) momap_del(l->parked, k);
            }
            ok = true;
            text = mfmt("job %s forgotten", w);
        }
        free(w);
        break;
    }
    }
    mj_req_free(&req);
    if (!ok) mj_outs_push(&out, mj_out_answer(token, false, text));
    else {
        mj_node_flush(n, &out);
        push_wait(n->lead, n->lead->n, token, true, text);
        mj_node_commit_check(n, &out);
    }
    mj_node_pump(n, now, unix_ms, &out);
    return out;
}

mj_outs mj_node_request(mj_node *n, mj_req req, uint64_t token, uint64_t now, uint64_t unix_ms) {
    return request_in(n, req, token, now, unix_ms, true);
}

/* The member side of payload results: each staged result's copies (`copies_for`): pushes to the
 * members that have not acknowledged one (again after PUSH_RESEND_MS), and the result to the outbox
 * once every copy is durable (its holders recorded). */
static void stage(mj_node *n, uint64_t now, mj_outs *out) {
    uint32_t me = n->cfg.me, cnt = n->cfg.n;
    uint32_t alive = n->view.alive | (uint32_t)1 << me;
    uint64_t k[3];
    uint64_t (*ready)[3] = NULL;
    size_t nready = 0;
    momap_iter cur8_;
    momap_iter_init(&cur8_, n->staged);
    const uint64_t *cur8_k;
    for (mj_staged *s = momap_iter_next(&cur8_, &cur8_k); s && (memcpy(k, cur8_k, sizeof k), 1); s = momap_iter_next(&cur8_, &cur8_k)) {
        if (!s->r.has_blob) continue;
        uint32_t want;
        if (!mj_copies_for(me, cnt, n->view.has_leader, n->view.leader, alive, s->refused, &want)) continue;
        if ((want & ~s->acked) == 0) {
            ready = mxrealloc(ready, (nready + 1) * sizeof *ready);
            memcpy(ready[nready++], k, sizeof k);
            continue;
        }
        uint32_t to = want & ~s->acked & ~((uint32_t)1 << me);
        for (uint32_t m = 0; m < 32; m++) {
            if (!(to >> m & 1)) continue;
            if ((s->sent_mask >> m & 1) && (now > s->sent[m] ? now - s->sent[m] : 0) < MESH_JOBS_PUSH_RESEND_MS) continue;
            s->sent_mask |= (uint32_t)1 << m;
            s->sent[m] = now > 1 ? now : 1;
            mj_outs_push(out, mj_out_push_blob(m, s->r.job, s->r.blob.hash));
        }
    }
    for (size_t i = 0; i < nready; i++) {
        mj_staged s;
        momap_take(n->staged, ready[i], &s);
        mj_item_res r = s.r;
        if (r.has_blob) {
            r.blob.holders = s.acked;
            mj_outs_push(out, mj_out_blob_done(r.job, r.blob.hash));
        }
        momap_del(n->outbox, ready[i]);
        mj_outbox_entry *e = momap_put(n->outbox, ready[i], NULL);
        e->r = r;
        e->at = 0;
    }
    free(ready);
}

mj_outs mj_node_tick(mj_node *n, uint64_t now, uint64_t unix_ms) {
    mj_outs out = {0};
    uint32_t me = n->cfg.me;
    if (n->lead && !n->lead->active && (now > n->lead->pull_at ? now - n->lead->pull_at : 0) >= 1000) {
        mj_lead *l = n->lead;
        l->pull_at = now;
        uint32_t ask = n->view.linked & ~l->responded & ~((uint32_t)1 << me);
        for (uint32_t k = 0; k < 32; k++) {
            if (!(ask >> k & 1)) continue;
            if ((l->heard_mask >> k & 1) && (now > l->heard[k] ? now - l->heard[k] : 0) < 3000) continue;
            mj_outs_push(&out, mj_out_send(k, mj_frame_pull(l->lid)));
        }
    }
    if (mj_node_active(n)) {
        /* Held submits: a member reports the program now, or their wait is over. */
        uint32_t alive = n->view.alive | (uint32_t)1 << me;
        mj_lead *l = n->lead;
        mj_held_submit *go = NULL;
        size_t ngo = 0, keep = 0;
        for (size_t i = 0; i < l->nheld; i++) {
            mj_held_submit *h = &l->held[i];
            if (now >= h->until || runs_mask(l, &h->s, alive)) {
                go = mxrealloc(go, (ngo + 1) * sizeof *go);
                go[ngo++] = *h;
            } else l->held[keep++] = *h;
        }
        l->nheld = keep;
        for (size_t i = 0; i < ngo; i++) {
            mj_req r = {.kind = MJ_REQ_SUBMIT, .submit = go[i].s};
            mj_outs o = request_in(n, r, go[i].token, now, unix_ms, false);
            mj_outs_append(&out, &o);
        }
        free(go);
        mj_node_dispatch(n, now, unix_ms, &out);
        mj_node_flush(n, &out);
        mj_node_snapshots(n, &out);
        mj_node_commit_check(n, &out);
    }
    /* The member side: availability, results, pruning. */
    if (n->view.has_leader) {
        uint32_t leader = n->view.leader;
        if (n->avail_dirty || (now > n->avail_at ? now - n->avail_at : 0) >= MESH_JOBS_AVAIL_MS) {
            n->avail_dirty = false;
            n->avail_at = now;
            size_t nh = momap_len(n->held), i = 0;
            mj_held *held = nh ? mxalloc(nh * sizeof *held) : NULL;
            uint64_t k;
            momap_iter cur9_;
            momap_iter_init(&cur9_, n->held);
            for (const mj_held *h = momap_iter_next1(&cur9_, &k); h; h = momap_iter_next1(&cur9_, &k)) held[i++] = mj_held_clone(h);
            mj_node_send(n, leader, mj_frame_avail(mj_node_lid(n), mj_progs_clone(n->progs, n->nprogs), n->nprogs, held, nh), &out);
        }
        stage(n, now, &out);
        /* Results due: never sent, or not confirmed for RESEND_MS; in frames of 4096. */
        uint64_t k[3];
        mj_item_res *batch = NULL;
        size_t nb = 0;
        momap_iter cur10_;
        momap_iter_init(&cur10_, n->outbox);
        const uint64_t *cur10_k;
        for (mj_outbox_entry *e = momap_iter_next(&cur10_, &cur10_k); e && (memcpy(k, cur10_k, sizeof k), 1); e = momap_iter_next(&cur10_, &cur10_k)) {
            if (!(e->at == 0 || (now > e->at ? now - e->at : 0) >= MESH_JOBS_RESEND_MS)) continue;
            e->at = now > 1 ? now : 1;
            if (!batch) batch = mxalloc(4096 * sizeof *batch);
            batch[nb++] = mj_item_res_clone(&e->r);
            if (nb == 4096) {
                mj_node_send(n, leader, mj_frame_result(mj_node_lid(n), batch, nb), &out);
                batch = NULL;
                nb = 0;
            }
        }
        if (nb) mj_node_send(n, leader, mj_frame_result(mj_node_lid(n), batch, nb), &out);
        else free(batch);
    }
    if ((now > n->prune_at ? now - n->prune_at : 0) >= 250) {
        n->prune_at = now;
        const mj_table *t = &n->rep.committed;
        uint64_t k[3];
        for (mj_outbox_entry *e = momap_first(n->outbox, k); e; e = momap_next(n->outbox, k, k)) {
            const mj_job *j = mj_table_get(t, k[0]);
            bool keep = j ? !mj_job_settled(j, k[1]) && mj_job_active(j) : !mj_table_forgot(t, k[0]);
            if (!keep) momap_del(n->outbox, k);
        }
        /* A staged payload whose item is settled meanwhile (another run's result committed) is not needed. */
        for (mj_staged *s = momap_first(n->staged, k); s; s = momap_next(n->staged, k, k)) {
            const mj_job *j = mj_table_get(t, k[0]);
            bool gone = j ? mj_job_settled(j, k[1]) || !mj_job_active(j) : mj_table_forgot(t, k[0]);
            if (!gone) continue;
            mj_staged x;
            momap_take(n->staged, k, &x);
            if (x.r.has_blob) mj_outs_push(&out, mj_out_blob_done(x.r.job, x.r.blob.hash));
            mj_item_res_free(&x.r);
        }
    }
    mj_node_pump(n, now, unix_ms, &out);
    return out;
}
