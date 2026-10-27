/* The jobs thread's publishing (jobd.rs `publish`, `news`, `jobs_recs`): the host block's S_JOB_*
 * fields, the jobs view's records and blobd's news, every 250 ms. */
#include "jobs_thread.h"

#include "mesh/abi/host_jobs.h"
#include "mesh/assemble.h"
#include "mesh/jobd/text.h"
#include "mesh/jobs/lid.h"
#include "mesh/strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t leased_of(const momap *ls, mj_job_id job, bool any_member, uint32_t member) {
    uint64_t s = 0;
    momap_iter it;
    momap_iter_init(&it, ls);
    for (const mj_lease *l = momap_iter_next(&it, NULL); l; l = momap_iter_next(&it, NULL))
        if (l->job == job && (any_member || l->member == member)) s += mj_ranges_len(&l->out);
    return s;
}

void jd_publish(jd *x) {
    mesh_host_block *h = &x->sh->host;
    const mj_node *n = x->node;
    const mj_table *t = &n->rep.committed;
    uint32_t active = 0;
    uint64_t done = 0, failed = 0, dupes = 0, id;
    for (mj_job *j = mj_table_first(t, &id); j; j = mj_table_next(t, &id)) {
        if (mj_job_active(j)) active++;
        uint64_t d = mj_ranges_len(&j->done);
        done = done + d < done ? UINT64_MAX : done + d;
        failed += momap_len(j->failed);
        dupes += j->dupes;
    }
    mesh_host_block_set_u32(h, MESH_S_JOBS_ACTIVE, active);
    mesh_host_block_set_u32(h, MESH_S_JOBS_TOTAL, (uint32_t)momap_len(t->jobs));
    mesh_host_block_set_u64(h, MESH_S_JOB_DONE, done);
    mesh_host_block_set_u64(h, MESH_S_JOB_FAILED, failed);
    mesh_host_block_set_u64(h, MESH_S_JOB_DUPES, dupes);
    uint64_t leased = 0;
    const momap *ls = mj_node_leases(n);
    momap_iter it;
    if (ls) {
        momap_iter_init(&it, ls);
        for (const mj_lease *l = momap_iter_next(&it, NULL); l; l = momap_iter_next(&it, NULL)) leased += mj_ranges_len(&l->out);
    } else {
        momap_iter_init(&it, n->held);
        for (const mj_held *l = momap_iter_next(&it, NULL); l; l = momap_iter_next(&it, NULL)) leased += mj_ranges_len(&l->items);
    }
    mesh_host_block_set_u32(h, MESH_S_JOB_LEASED, leased > UINT32_MAX ? UINT32_MAX : (uint32_t)leased);
    double rate = 0;
    const mj_mpmap *rates = mj_node_rates(n);
    if (rates)
        for (size_t i = 0; i < rates->n; i++) rate += rates->v[i].rate.ips;
    else rate = mj_node_my_rate(n);
    mesh_host_block_set_f32(h, MESH_S_JOB_RATE, (float)rate);
    mesh_host_block_set_u64(h, MESH_S_JOB_COMMIT, mj_node_commit_n(n));
    mesh_host_block_set_u64(h, MESH_S_JOB_TERM, mj_lid_term(mj_node_lid(n)));
    mesh_host_block_set_u32(h, MESH_S_JOB_FLAGS, (mj_node_leading(n) ? MESH_JF_LEADER : 0) | (mj_node_active(n) ? MESH_JF_ACTIVE : 0) |
                                                    (mjd_store_persisted(x->d->store) ? MESH_JF_PERSIST : 0));
    mesh_host_block_set_u32(h, MESH_S_JOB_HELD, (uint32_t)momap_len(n->held));
    mesh_recs recs = jd_jobs_recs(x, rate);
    pthread_mutex_lock(&x->sh->mu);
    mesh_recs_free(&x->sh->jobs_view_recs);
    x->sh->jobs_view_recs = recs;
    pthread_mutex_unlock(&x->sh->mu);

    /* blobd's news: every job's state, the jobs forgotten since, and the committed payload results of the
     * images it assembles that it does not have yet. */
    mbd_news *nw = calloc(1, sizeof *nw);
    if (!nw) abort();
    size_t cap = momap_len(t->jobs);
    nw->jobs = mxalloc((cap ? cap : 1) * sizeof *nw->jobs);
    for (mj_job *j = mj_table_first(t, &id); j; j = mj_table_next(t, &id))
        nw->jobs[nw->njobs++] = (mbd_job_info){j->spec.id, mstrdup(j->spec.program), j->spec.op, mj_setup_clone(j->spec.setup, j->spec.nsetup),
                                                j->spec.nsetup, j->tone, mj_job_active(j), mstrdup(mj_job_state(j))};
    for (size_t i = 0; i < t->nforgotten; i++) {
        bool fresh;
        momap_put1(x->told, t->forgotten[i], &fresh);
        if (!fresh) continue;
        nw->forgotten = mxrealloc(nw->forgotten, (nw->nforgotten + 1) * sizeof *nw->forgotten);
        nw->forgotten[nw->nforgotten++] = t->forgotten[i];
    }
    mj_job_id *watch;
    size_t nwatch = mjd_ext_watch(x->sh, &watch);
    uint64_t k;
    for (void *v = momap_first(x->sent, &k); v; v = momap_next(x->sent, &k, &k)) {
        bool kept = false;
        for (size_t i = 0; i < nwatch; i++) kept |= watch[i] == k;
        if (!kept) momap_del1(x->sent, k);
    }
    size_t capr = 0;
    for (size_t w = 0; w < nwatch; w++) {
        mj_job *j = mj_table_get(t, watch[w]);
        if (!j) continue;
        mj_ranges *sent = momap_put1(x->sent, watch[w], NULL);
        uint64_t item;
        momap_iter_init(&it, j->results);
        for (const mj_res *r = momap_iter_next1(&it, &item); r; r = momap_iter_next1(&it, &item)) {
            uint64_t part;
            if (mj_ranges_contains(sent, item)) continue;
            if (!r->has_blob || !masm_part_of(r->out, r->out_len, &part)) continue;
            mj_ranges_add(sent, item);
            if (nw->nresults == capr) nw->results = mxrealloc(nw->results, (capr = capr ? 2 * capr : 64) * sizeof *nw->results);
            nw->results[nw->nresults++] = (mbd_result){watch[w], item, {r->blob.hash, r->blob.len, r->blob.holders, r->member, part}};
        }
    }
    free(watch);
    mjd_blobs_send(x->sh, &(mjd_bev){.k = MJD_BEV_TABLE, .news = nw});
}

/* The jobs view's records, from this member's committed table (and, on the leader, its leases, rates
 * and picks):
 * - `jobs`: system, leader, role, commit, jobs, active, leases held here, unconfirmed results, items/s, persisted;
 * - `job` per job: program, state, priority, items, done, failed, leased, queued, dupes, retried items, mode,
 *   progress (0..1; endless: -), items/s over the last ~3 s, ETA, elapsed; text: the cancel reason;
 * - `share` per (job, member that ran or holds some): done, leased now, the member's measured rate of the program;
 * - `pick` per (job, member) on the leader: the lease choice (mj_select); text: the score's terms or the reason;
 * - `result` (the last 3 per job) and `failed` (the first 5 per job): the preview. */
mesh_recs jd_jobs_recs(jd *x, double rate) {
    const mj_node *n = x->node;
    const mj_table *t = &n->rep.committed;
    mj_view v = jd_view(x);
    int64_t now = mesh_mono_ns();
    uint64_t unix_ms = jd_unix_ms();
    mesh_recs out = MESH_RECS_INIT;
    mjd_text_cx cx = {n, v, x->peered, unix_ms};
    uint64_t id;
    uint32_t active = 0;
    for (mj_job *j = mj_table_first(t, &id); j; j = mj_table_next(t, &id)) active += mj_job_active(j);
    mesh_rec *r = mesh_recs_add(&out, "jobs");
    mesh_rec_kvu(r, "system", x->d->cfg.me);
    if (v.has_leader) mesh_rec_kvu(r, "leader", v.leader);
    else mesh_rec_none(r, "leader");
    mesh_rec_kv(r, "role", mjd_role(&cx));
    mesh_rec_kvu(r, "commit", mj_node_commit_n(n));
    mesh_rec_kvu(r, "jobs", momap_len(t->jobs));
    mesh_rec_kvu(r, "active", active);
    mesh_rec_kvu(r, "held", momap_len(n->held));
    mesh_rec_kvu(r, "unconfirmed", mj_node_unconfirmed(n));
    mesh_rec_f(r, "rate", true, rate, 1);
    mesh_rec_kvu(r, "persisted", mjd_store_persisted(x->d->store) ? 1 : 0);
    for (void *hv = momap_first(x->hist, &id); hv; hv = momap_next(x->hist, &id, &id))
        if (!mj_table_get(t, id)) momap_del1(x->hist, id);
    const momap *ls = mj_node_leases(n);
    const mj_mpmap *rates = mj_node_rates(n);
    for (mj_job *j = mj_table_first(t, &id); j; j = mj_table_next(t, &id)) {
        char word[MJ_JOB_WORD_MAX];
        mj_job_word(id, word);
        uint64_t done = mj_ranges_len(&j->done), nfailed = momap_len(j->failed);
        bool act = mj_job_active(j);
        jd_hist *h = momap_put1(x->hist, id, NULL);
        if (h->n == h->cap) {
            h->cap = h->cap ? 2 * h->cap : 8;
            h->at = mxrealloc(h->at, h->cap * sizeof *h->at);
            h->done = mxrealloc(h->done, h->cap * sizeof *h->done);
        }
        h->at[h->n] = now;
        h->done[h->n++] = done;
        while (h->n > 2 && now - h->at[1] >= 3000000000LL) {
            memmove(h->at, h->at + 1, (h->n - 1) * sizeof *h->at);
            memmove(h->done, h->done + 1, (h->n - 1) * sizeof *h->done);
            h->n--;
        }
        double ips = 0;
        if (h->n && h->at[h->n - 1] > h->at[0] && act)
            ips = (double)(h->done[h->n - 1] - h->done[0]) / ((double)(h->at[h->n - 1] - h->at[0]) / 1e9);
        bool endless = mj_spec_endless(&j->spec);
        uint64_t total = mj_job_total(j);
        uint64_t settled = done + nfailed;
        uint64_t left = total > settled ? total - settled : 0;
        bool has_eta = !endless && act && ips > 0;
        r = mesh_recs_add(&out, "job");
        mesh_rec_kv(r, "job", word);
        mesh_rec_kv(r, "program", j->spec.program);
        mesh_rec_kv(r, "state", mj_job_state(j));
        mesh_rec_kvi(r, "priority", j->spec.priority);
        if (endless) mesh_rec_kv(r, "items", "endless");
        else mesh_rec_kvu(r, "items", total);
        mesh_rec_kvu(r, "done", done);
        mesh_rec_kvu(r, "failed", nfailed);
        if (ls) mesh_rec_kvu(r, "leased", leased_of(ls, id, true, 0));
        else mesh_rec_none(r, "leased");
        uint64_t q;
        if (!mj_node_queued(n, id, &q)) mesh_rec_none(r, "queued");
        else if (endless) mesh_rec_kv(r, "queued", "endless");
        else mesh_rec_kvu(r, "queued", q);
        mesh_rec_kvu(r, "dupes", j->dupes);
        mesh_rec_kvu(r, "retried", momap_len(j->fails));
        mesh_rec_kv(r, "mode", mj_mode_word(j->spec.mode));
        mesh_rec_f(r, "progress", !endless, (double)settled / (double)(total ? total : 1), 4);
        mesh_rec_f(r, "rate", true, ips, 1);
        mesh_rec_f(r, "eta_s", has_eta, has_eta ? (double)left / ips : 0, 1);
        uint64_t end = j->finished ? j->finish_at : unix_ms;
        mesh_rec_kvu(r, "elapsed_ms", end > j->spec.submitted_ms ? end - j->spec.submitted_ms : 0);
        mesh_rec_kvu(r, "submitted_ms", j->spec.submitted_ms);
        mesh_rec_text(r, j->cancel ? j->cancel : "");
        /* The members that ran some of it, and those that hold some now. */
        uint32_t members = 0;
        uint64_t m;
        momap_iter it;
        momap_iter_init(&it, j->by_member);
        for (void *c = momap_iter_next1(&it, &m); c; c = momap_iter_next1(&it, &m))
            if (m < 32) members |= 1u << m;
        if (ls) {
            momap_iter_init(&it, ls);
            for (const mj_lease *l = momap_iter_next(&it, NULL); l; l = momap_iter_next(&it, NULL))
                if (l->job == id && l->member < 32) members |= 1u << l->member;
        }
        for (uint32_t k = 0; k < 32; k++) {
            if (!(members >> k & 1)) continue;
            uint64_t held = ls ? leased_of(ls, id, false, k) : 0;
            /* The rate while it holds items of the job (an idle member's last rate is history). */
            bool has_rate = rates && held > 0;
            double kr = 0;
            if (has_rate)
                for (const char *p = j->spec.program; p;) {
                    const char *c = strchr(p, ',');
                    char *name = c ? mstrndup(p, (size_t)(c - p)) : mstrdup(p);
                    const mj_mp *mp = mj_mpmap_get(rates, k, name);
                    if (mp) kr += mp->rate.ips;
                    free(name);
                    p = c ? c + 1 : NULL;
                }
            r = mesh_recs_add(&out, "share");
            mesh_rec_kv(r, "job", word);
            mesh_rec_kvu(r, "member", k);
            mesh_rec_kvu(r, "done", mj_count_get(j->by_member, k));
            if (ls) mesh_rec_kvu(r, "leased", held);
            else mesh_rec_none(r, "leased");
            mesh_rec_f(r, "rate", has_rate, kr, 1);
        }
        const mj_picks *p = mj_node_picks(n, id);
        for (size_t i = 0; p && i < p->n; i++) {
            const mj_pick *x1 = &p->v[i];
            r = mesh_recs_add(&out, "pick");
            mesh_rec_kv(r, "job", word);
            mesh_rec_kvu(r, "member", x1->member);
            mesh_rec_kv(r, "verdict", x1->ok ? "eligible" : "rejected");
            mesh_rec_kvu(r, "busy", x1->busy ? 1 : 0);
            mesh_rec_f(r, "score", x1->ok, x1->ok ? x1->score.total : 0, 3);
            mesh_rec_text(r, x1->ok ? (x1->score.explain ? x1->score.explain : "") : (x1->why ? x1->why : ""));
        }
        uint64_t item;
        int c = 0;
        for (const mj_res *res = momap_last(j->results, &item); res && c < 3; res = momap_prev(j->results, &item, &item), c++) {
            char hex[33];
            size_t nb = res->out_len < 16 ? res->out_len : 16;
            for (size_t b = 0; b < nb; b++) snprintf(hex + 2 * b, 3, "%02x", res->out[b]);
            hex[2 * nb] = 0;
            r = mesh_recs_add(&out, "result");
            mesh_rec_kv(r, "job", word);
            mesh_rec_kvu(r, "item", item);
            mesh_rec_kvu(r, "result", res->result);
            mesh_rec_kvu(r, "member", res->member);
            mesh_rec_kv(r, "out", hex);
        }
        c = 0;
        momap_iter_init(&it, j->failed);
        for (const mj_failed *f = momap_iter_next1(&it, &item); f && c < 5; f = momap_iter_next1(&it, &item), c++) {
            r = mesh_recs_add(&out, "failed");
            mesh_rec_kv(r, "job", word);
            mesh_rec_kvu(r, "item", item);
            mesh_rec_kvu(r, "tries", f->tries);
            mesh_rec_text(r, f->why ? f->why : "");
        }
    }
    return out;
}
