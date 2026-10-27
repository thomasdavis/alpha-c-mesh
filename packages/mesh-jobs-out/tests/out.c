/* durable: persists before sends and answers, one sync per run of persists, and fail-stop (jobs.rs's
 * `nothing_leaves_without_its_persist`, the durable half; the node half is in @ajax/mesh-jobs-node). */
#include "mesh/jobs/out.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int appends, syncs, fail_append, fail_sync;
    char trace[64];
} disk;

static bool app(void *c, const mj_log *l, char **err) {
    disk *d = c;
    (void)l;
    d->appends++;
    strcat(d->trace, "a");
    if (d->appends == d->fail_append) {
        *err = mstrdup("ENOSPC");
        return false;
    }
    return true;
}

static bool syn(void *c, char **err) {
    disk *d = c;
    d->syncs++;
    strcat(d->trace, "s");
    if (d->fail_sync) {
        *err = mstrdup("EIO");
        return false;
    }
    return true;
}

static mj_outs sample(void) {
    mj_outs v = {0};
    mj_outs_push(&v, mj_out_persist(mj_log_lid(1)));
    mj_outs_push(&v, mj_out_persist(mj_log_commit(1, 2)));
    mj_outs_push(&v, mj_out_log(mstrdup("x")));
    mj_outs_push(&v, mj_out_send(0, mj_frame_ack(1, 2, false)));
    mj_outs_push(&v, mj_out_answer(7, true, mstrdup("ok")));
    mj_outs_push(&v, mj_out_persist(mj_log_lid(2)));
    mj_outs_push(&v, mj_out_cut(5, 0));
    return v;
}

int main(void) {
    disk d = {0};
    mj_disk k = {&d, app, syn};
    mj_outs in = sample();
    bool ok;
    char *err = NULL;
    mj_outs got = mj_durable(&in, &k, &ok, &err);
    CHECK(ok && got.n == 4);
    CHECK_S(d.trace, "aasa" "s"); /* two appends, a sync before the send, the last append, a sync at the end */
    CHECK(got.v[0].kind == MJ_OUT_LOG && got.v[1].kind == MJ_OUT_SEND && got.v[2].kind == MJ_OUT_ANSWER && got.v[3].kind == MJ_OUT_CUT);
    CHECK(got.v[1].frame.ty == MESH_F_JOB_ACK && got.v[1].frame.n == 2);
    mj_outs_free(&got);
    /* The second append fails: nothing after the first append is acted on. */
    disk d2 = {.fail_append = 2};
    k.ctx = &d2;
    in = sample();
    got = mj_durable(&in, &k, &ok, &err);
    CHECK(!ok && got.n == 0);
    CHECK_S(err, "ENOSPC");
    free(err);
    mj_outs_free(&got);
    /* The sync before the send fails: the log line before it went, the send did not. */
    disk d3 = {.fail_sync = 1};
    k.ctx = &d3;
    in = sample();
    got = mj_durable(&in, &k, &ok, &err);
    CHECK(!ok && got.n == 1 && got.v[0].kind == MJ_OUT_LOG);
    CHECK_S(err, "EIO");
    free(err);
    mj_outs_free(&got);
    return mesh_check_done();
}
