/* Job items (plan F5): leases queued per slot, each job's setup first, items published as OP tickets. */
#include "driver_int.h"

#include <stdlib.h>
#include <string.h>

/* Job item tickets in flight per slot: two per node keep every node fed (Metal: `fanout`, one command buffer). */
uint32_t drv_job_cap(const mesh_manifest *m, uint32_t exec) {
    if (exec == MESH_BACKEND_METAL) return m->fanout > 1 ? m->fanout : 1;
    uint32_t c = 2 * m->nodes;
    if (c < 2) c = 2;
    if (c > MESH_PROG_RING / 2) c = MESH_PROG_RING / 2;
    return c;
}

void drv_lease_free(dlease *l) {
    free(l->setup);
    mj_ranges_free(&l->items);
    memset(l, 0, sizeof *l);
}

/* An item's outcome to the jobs thread. */
void drv_job_done(mesh_driver *d, uint64_t lease, uint64_t job, uint64_t item, uint32_t status, uint32_t result, const char *out) {
    mesh_job_event e = mesh_job_event_new(MESH_JEV_DONE);
    e.res.lease = lease;
    e.res.job = job;
    e.res.item = item;
    e.res.status = status;
    e.res.result = result;
    e.res.out_len = strlen(out);
    e.res.out = mesh_xmalloc(e.res.out_len ? e.res.out_len : 1);
    memcpy(e.res.out, out, e.res.out_len);
    if (mesh_chan_send(d->sh->jobs, &e) != MESH_CHAN_OK) mesh_job_event_free(&e);
}

/* Every item of a lease (at most a million per range, as the Rust) reported with status and why. */
void drv_jobs_returned(mesh_driver *d, const dlease *l, uint32_t status, const char *why) {
    for (size_t r = 0; r < l->items.n; r++) {
        uint64_t a = l->items.v[r][0], b = l->items.v[r][1];
        uint64_t end = b < a + 1000000 ? b : a + 1000000;
        for (uint64_t item = a; item < end; item++) drv_job_done(d, l->lease, l->job, item, status, 0, why);
    }
}

/* Slot s's next job items: its leases in order, each job's setup first (nothing of another job in flight),
 * the item's 2^32-window base in the view's `job_base`, at most job_cap tickets in flight. */
void drv_publish_jobs(mesh_driver *d, uint32_t s) {
    mesh_mailbox mb = d->sh->mb;
    dslot *x = d->slots[s];
    uint32_t cap = drv_job_cap(&x->image.manifest, x->image.exec);
    djobslot *j = &d->jobw[s];
    dlease *failed = NULL;
    char **why = NULL;
    size_t nfailed = 0;
    while (j->n) {
        dlease *front = &j->q[0];
        uint64_t item;
        if (!mj_ranges_first(&front->items, &item)) {
            drv_lease_free(front);
            memmove(j->q, j->q + 1, (j->n - 1) * sizeof *j->q);
            j->n--;
            continue;
        }
        uint64_t job = front->job, window = item & ~0xffffffffull;
        if (!j->has_ctx || j->ctx_job != job || j->ctx_window != window) {
            /* Another job or window: what is in flight completes first. */
            if (j->inflight > 0 || j->setup.busy) break;
            if (!j->has_ctx || j->ctx_job != job) {
                mesh_setup_step st = mesh_setup_state_step(&j->setup, job, front->setup, front->nsetup);
                if (st.k == MESH_SETUP_WAIT) break;
                if (st.k == MESH_SETUP_PUBLISH) {
                    /* One setup task at a time: the next is published when this one completes. */
                    mesh_kind k = mesh_kind_of(MESH_TK_JOB_SETUP);
                    k.job = job;
                    k.op = st.op;
                    k.arg = st.arg;
                    if (mesh_pring_can_publish(&x->ring) && mesh_pring_publish(&x->ring, mb, st.op, st.arg, k, NULL)) mesh_setup_state_published(&j->setup);
                    break;
                }
                if (st.k == MESH_SETUP_FAILED) {
                    /* The setup failed: so does every queued item of the job here (a try each). */
                    char *e = mesh_xstrdup(st.err);
                    size_t k = 0;
                    for (size_t i = 0; i < j->n; i++) {
                        if (j->q[i].job == job) {
                            failed = mesh_xrealloc(failed, (nfailed + 1) * sizeof *failed);
                            why = mesh_xrealloc(why, (nfailed + 1) * sizeof *why);
                            failed[nfailed] = j->q[i];
                            why[nfailed] = mesh_xstrdup(e);
                            nfailed++;
                        } else j->q[k++] = j->q[i];
                    }
                    j->n = k;
                    free(e);
                    mesh_setup_state_reset(&j->setup);
                    continue;
                }
            }
            if (x->image.manifest.has_job_base) mesh_mailbox_set64(mb, (size_t)x->view_off + x->image.manifest.job_base, window);
            j->has_ctx = true;
            j->ctx_job = job;
            j->ctx_window = window;
        }
        if (j->inflight >= cap || !mesh_pring_can_publish(&x->ring)) break;
        mj_ranges_take(&front->items, item);
        mesh_kind k = mesh_kind_of(MESH_TK_JOB);
        k.lease = front->lease;
        k.job = job;
        k.item = item;
        if (!mesh_pring_publish(&x->ring, mb, front->op, (uint32_t)item, k, NULL)) { mj_ranges_add(&front->items, item); break; }
        j->inflight++;
    }
    for (size_t i = 0; i < nfailed; i++) {
        drv_jobs_returned(d, &failed[i], MESH_JOBS_RS_FAIL, why[i]);
        drv_lease_free(&failed[i]);
        free(why[i]);
    }
    free(failed);
    free(why);
}
