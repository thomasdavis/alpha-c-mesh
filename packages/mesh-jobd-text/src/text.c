/* jobd.rs's jobs_text, job_line, status_text and results_text. */
#include "mesh/jobd/text.h"
#include "mesh/jobs/lid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *mjd_role(const mjd_text_cx *cx) {
    if (mj_node_active(cx->node)) return "leader";
    if (mj_node_leading(cx->node)) return "leader-merging";
    return cx->peered ? "follower" : "single";
}

static void leader_word(const mj_view *v, char out[16]) {
    if (v->has_leader) snprintf(out, 16, "%u", v->leader);
    else strcpy(out, "-");
}

/* Items of job id in the leader's leases (false: not leading). */
static bool leased_of(const mj_node *n, mj_job_id id, uint64_t *sum) {
    const momap *ls = mj_node_leases(n);
    if (!ls) return false;
    *sum = 0;
    momap_iter it;
    momap_iter_init(&it, ls);
    for (const mj_lease *x = momap_iter_next(&it, NULL); x; x = momap_iter_next(&it, NULL))
        if (x->job == id) *sum += mj_ranges_len(&x->out);
    return true;
}

void mjd_job_line(const mjd_text_cx *cx, const mj_job *j, msb *out) {
    mj_job_id id = j->spec.id;
    uint64_t leased, queued, end = j->finished ? j->finish_at : cx->unix_ms;
    uint64_t elapsed = end > j->spec.submitted_ms ? end - j->spec.submitted_ms : 0;
    char w[MJ_JOB_WORD_MAX];
    mj_job_word(id, w);
    msb_printf(out, "job %s program %s state %s priority %d items ", w, j->spec.program, mj_job_state(j), j->spec.priority);
    if (mj_spec_endless(&j->spec)) msb_puts(out, "endless");
    else msb_printf(out, "%llu", (unsigned long long)mj_job_total(j));
    uint64_t done = mj_ranges_len(&j->done);
    msb_printf(out, " done %llu failed %zu leased ", (unsigned long long)done, momap_len(j->failed));
    if (leased_of(cx->node, id, &leased)) msb_printf(out, "%llu", (unsigned long long)leased);
    else msb_putc(out, '-');
    msb_puts(out, " queued ");
    if (mj_node_queued(cx->node, id, &queued)) msb_printf(out, "%llu", (unsigned long long)queued);
    else msb_putc(out, '-');
    msb_printf(out, " dupes %llu mode %s elapsed_ms %llu rate %.1f\n", (unsigned long long)j->dupes, mj_mode_word(j->spec.mode),
               (unsigned long long)elapsed, (double)done * 1000.0 / (double)(elapsed ? elapsed : 1));
}

char *mjd_jobs_text(const mjd_text_cx *cx) {
    const mj_node *n = cx->node;
    const mj_table *t = mj_node_committed(n);
    size_t jobs = 0, active = 0;
    uint64_t id;
    for (const mj_job *j = mj_table_first(t, &id); j; j = mj_table_next(t, &id)) {
        jobs++;
        active += mj_job_active(j);
    }
    char lw[16];
    leader_word(&cx->view, lw);
    msb b = {0};
    msb_printf(&b, "jobs system %u leader %s role %s lid %llx commit %llu jobs %zu active %zu held %zu unconfirmed %zu staged %zu parked %zu rate %.1f\n",
               n->cfg.me, lw, mjd_role(cx), (unsigned long long)mj_node_lid(n), (unsigned long long)mj_node_commit_n(n), jobs, active,
               momap_len(n->held), mj_node_unconfirmed(n), mj_node_staged(n), mj_node_parked(n), mj_node_my_rate(n));
    for (const mj_job *j = mj_table_first(t, &id); j; j = mj_table_next(t, &id)) mjd_job_line(cx, j, &b);
    const mj_mpmap *rates = mj_node_rates(n);
    if (rates)
        for (size_t i = 0; i < rates->n; i++)
            msb_printf(&b, "rate system %u program %s items_per_s %.1f\n", rates->v[i].member, rates->v[i].program, rates->v[i].rate.ips);
    return msb_take(&b);
}

static bool no_job(mj_job_id id, char **text) {
    char w[MJ_JOB_WORD_MAX];
    mj_job_word(id, w);
    *text = mfmt("no job %s (committed here)", w);
    return false;
}

bool mjd_status_text(const mjd_text_cx *cx, mj_job_id id, char **text) {
    const mj_job *j = mj_table_get(mj_node_committed(cx->node), id);
    if (!j) return no_job(id, text);
    msb b = {0};
    mjd_job_line(cx, j, &b);
    msb_puts(&b, "spec ");
    mj_spec_encode(&j->spec, &b);
    msb_putc(&b, '\n');
    /* Per member: who ran how many (committed). */
    uint64_t k;
    for (const uint64_t *c = momap_first(j->by_member, &k); c; c = momap_next(j->by_member, &k, &k))
        msb_printf(&b, "member %llu done %llu\n", (unsigned long long)k, (unsigned long long)*c);
    const momap *ls = mj_node_leases(cx->node);
    if (ls) {
        uint64_t lk;
        for (const mj_lease *x = momap_first(ls, &lk); x; x = momap_next(ls, &lk, &lk)) {
            if (x->job != id) continue;
            msb_printf(&b, "lease %llx system %u items %llu left %llu (", (unsigned long long)x->id, x->member, (unsigned long long)x->size,
                       (unsigned long long)mj_ranges_len(&x->out));
            mj_ranges_word(&x->out, &b);
            msb_puts(&b, ")\n");
        }
    }
    /* The lease choice (F4's filter and score), as the leader made it last. */
    const mj_picks *p = mj_node_picks(cx->node, id);
    if (p)
        for (size_t i = 0; i < p->n; i++) {
            char *l = mj_pick_line(&p->v[i]);
            msb_printf(&b, "%s\n", l);
            free(l);
        }
    else if (mj_job_active(j)) {
        char lw[16];
        leader_word(&cx->view, lw);
        msb_printf(&b, "pick on the leader (system %s): its `job status` explains the lease choice\n", lw);
    }
    if (j->cancel) msb_printf(&b, "cancel %s\n", j->cancel);
    size_t shown = 0;
    for (const mj_failed *f = momap_first(j->failed, &k); f && shown < 50; f = momap_next(j->failed, &k, &k), shown++)
        msb_printf(&b, "failed %llu tries %u %s\n", (unsigned long long)k, f->tries, f->why);
    size_t retried = momap_len(j->fails);
    if (retried > 0) msb_printf(&b, "retried %zu item(s)\n", retried);
    *text = msb_take(&b);
    return true;
}

bool mjd_results_text(const mjd_text_cx *cx, mj_job_id id, uint64_t from, uint64_t n, char **text) {
    const mj_job *j = mj_table_get(mj_node_committed(cx->node), id);
    if (!j) return no_job(id, text);
    msb s = {0};
    uint64_t c = 0, i = 0, item;
    momap_iter it;
    momap_iter_init(&it, j->results);
    for (const mj_res *r = momap_iter_next1(&it, &item); r && c < n; r = momap_iter_next1(&it, &item), i++) {
        if (i < from) continue;
        msb_printf(&s, "%llu %u lease %llx member %u", (unsigned long long)item, r->result, (unsigned long long)r->lease, r->member);
        if (r->out_len) {
            msb_puts(&s, " out ");
            for (size_t q = 0; q < r->out_len; q++) msb_printf(&s, "%02x", r->out[q]);
        }
        if (r->has_blob) {
            char x[65];
            mj_hex(&r->blob.hash, x);
            msb_printf(&s, " blob %s len %u holders 0x%x", x, r->blob.len, r->blob.holders);
        }
        msb_putc(&s, '\n');
        c++;
    }
    char w[MJ_JOB_WORD_MAX];
    mj_job_word(id, w);
    msb b = {0};
    msb_printf(&b, "results %s total %zu from %llu count %llu done %llu failed %zu dupes %llu state %s\n%s", w, momap_len(j->results),
               (unsigned long long)from, (unsigned long long)c, (unsigned long long)mj_ranges_len(&j->done), momap_len(j->failed),
               (unsigned long long)j->dupes, mj_job_state(j), msb_str(&s));
    msb_free(&s);
    *text = msb_take(&b);
    return true;
}
