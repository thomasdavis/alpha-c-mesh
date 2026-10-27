/* jobs.rs's Out and durable. */
#include "mesh/jobs/out.h"

#include <stdlib.h>
#include <string.h>

void mj_out_free(mj_out *o) {
    mj_frame_free(&o->frame);
    mj_log_free(&o->log);
    if (o->kind == MJ_OUT_RUN) mj_lease_msg_free(&o->run);
    free(o->text);
    memset(o, 0, sizeof *o);
}

void mj_outs_push(mj_outs *v, mj_out o) {
    if (v->n == v->cap) {
        v->cap = v->cap ? 2 * v->cap : 16;
        v->v = mxrealloc(v->v, v->cap * sizeof *v->v);
    }
    v->v[v->n++] = o;
}

void mj_outs_append(mj_outs *dst, mj_outs *src) {
    for (size_t i = 0; i < src->n; i++) mj_outs_push(dst, src->v[i]);
    free(src->v);
    *src = (mj_outs){0};
}

void mj_outs_free(mj_outs *v) {
    for (size_t i = 0; i < v->n; i++) mj_out_free(&v->v[i]);
    free(v->v);
    *v = (mj_outs){0};
}

mj_out mj_out_send(uint32_t to, mj_frame f) { return (mj_out){.kind = MJ_OUT_SEND, .to = to, .frame = f}; }
mj_out mj_out_push_blob(uint32_t to, mj_job_id job, mj_hash hash) { return (mj_out){.kind = MJ_OUT_PUSH_BLOB, .to = to, .job = job, .hash = hash}; }
mj_out mj_out_fetch_blob(mj_job_id job, mj_hash hash, uint32_t holders) {
    return (mj_out){.kind = MJ_OUT_FETCH_BLOB, .job = job, .hash = hash, .holders = holders};
}
mj_out mj_out_blob_done(mj_job_id job, mj_hash hash) { return (mj_out){.kind = MJ_OUT_BLOB_DONE, .job = job, .hash = hash}; }
mj_out mj_out_persist(mj_log l) { return (mj_out){.kind = MJ_OUT_PERSIST, .log = l}; }
mj_out mj_out_run(mj_lease_msg l) { return (mj_out){.kind = MJ_OUT_RUN, .run = l}; }
mj_out mj_out_cut(uint64_t lease, uint64_t cut) { return (mj_out){.kind = MJ_OUT_CUT, .lease = lease, .cut = cut}; }
mj_out mj_out_answer(uint64_t token, bool ok, char *text) { return (mj_out){.kind = MJ_OUT_ANSWER, .token = token, .ok = ok, .text = text}; }
mj_out mj_out_log(char *text) { return (mj_out){.kind = MJ_OUT_LOG, .text = text}; }

mj_outs mj_durable(mj_outs *outs, const mj_disk *disk, bool *ok, char **err) {
    mj_outs v = {0};
    bool dirty = false;
    *ok = true;
    size_t i = 0;
    for (; i < outs->n; i++) {
        mj_out *o = &outs->v[i];
        if (o->kind == MJ_OUT_PERSIST) {
            if (!disk->append(disk->ctx, &o->log, err)) {
                *ok = false;
                break;
            }
            dirty = true;
            mj_out_free(o);
            continue;
        }
        if ((o->kind == MJ_OUT_SEND || o->kind == MJ_OUT_ANSWER) && dirty) {
            if (!disk->sync(disk->ctx, err)) {
                *ok = false;
                break;
            }
            dirty = false;
        }
        mj_outs_push(&v, *o);
        memset(o, 0, sizeof *o);
    }
    if (*ok && dirty && !disk->sync(disk->ctx, err)) *ok = false;
    /* What was not acted on is dropped. */
    for (; i < outs->n; i++) mj_out_free(&outs->v[i]);
    free(outs->v);
    *outs = (mj_outs){0};
    return v;
}
