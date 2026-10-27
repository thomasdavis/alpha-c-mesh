/* Three blobd's over a fake network: an own payload kept and pushed (acknowledged), a fetch from a
 * holder, a corrupt push dropped, a forget dropping references, a full store refusing, on-disk writes
 * through the writers, and `render` of an assembled image. */
#include "mesh/blobd/blobd.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/blobs/durable.h"
#include "mesh/blobs/frame.h"
#include "pathtrace/default_scene.h"
#include "mesh/check.h"
#include "mesh/ref/sha256d.h"
#include "mesh/strbuf.h"
#include "pathtrace/scene.h"
#include "pathtrace/view.h"
#include "pathtrace/z.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define N 3

typedef struct {
    uint32_t from, to;
    uint8_t *b;
    size_t n;
} msg;

static msg q[100000];
static size_t nq;
static mbd *node[N];
static int64_t clock_ms = 1;
static int stored[N], acked_ok[N], acked_bad[N], gone[N];
static int64_t mem[N];
static bool watched[N][8];
static mbd_write pending[64];
static size_t npending;
static int errors;

typedef struct {
    uint32_t me;
} cx;
static cx cxs[N];

static bool send(void *c, uint32_t to, const uint8_t *f, size_t n) {
    msg *m = &q[nq++];
    m->from = ((cx *)c)->me;
    m->to = to;
    m->b = mxalloc(n);
    memcpy(m->b, f, n);
    m->n = n;
    return true;
}
static void on_stored(void *c, const mb_ref *r, size_t n) {
    (void)r;
    stored[((cx *)c)->me] += (int)n;
}
static void on_acked(void *c, uint32_t from, mj_job_id job, const mj_hash *h, bool ok) {
    (void)from, (void)job, (void)h;
    if (ok) acked_ok[((cx *)c)->me]++;
    else acked_bad[((cx *)c)->me]++;
}
static void on_gone(void *c, const mb_ref *r, size_t n) {
    (void)r;
    gone[((cx *)c)->me] += (int)n;
}
static uint32_t alive(void *c) {
    (void)c;
    return 7;
}
static bool leading(void *c) { return ((cx *)c)->me == 0; }
static void watching(void *c, mj_job_id job, bool on) { watched[((cx *)c)->me][job & 7] = on; }
static void on_mem(void *c, int64_t d) { mem[((cx *)c)->me] += d; }
static void on_write(void *c, mbd_write w) {
    (void)c;
    pending[npending++] = w;
}
static int64_t now(void *c) {
    (void)c;
    return clock_ms;
}
static void on_log(void *c, bool error, const char *line) {
    (void)c;
    if (error) errors++;
    (void)line;
}

/* Delivers every queued frame (frames sent while delivering go too). */
static void pump(void) {
    for (size_t i = 0; i < nq; i++) {
        msg m = q[i];
        uint32_t len;
        memcpy(&len, m.b + 1, 4);
        mbd_frame(node[m.to], m.from, m.b[0], m.b + 5, len);
        free(m.b);
    }
    nq = 0;
}

/* The writers: each pending write made durable, then reported to its node. */
static void writers(uint32_t k) {
    bool ok[64];
    char *err[64];
    mbd_write_batch(pending, npending, ok, err);
    for (size_t i = 0; i < npending; i++) {
        mbd_written(node[k], pending[i].job, pending[i].hash, pending[i].why, pending[i].from, ok[i], err[i]);
        free(err[i]);
        mbd_write_free(&pending[i]);
    }
    npending = 0;
}

static mj_hash sha(const void *b, size_t n) {
    mj_hash h;
    mesh_sha256(b, n, h.b);
    return h;
}

int main(void) {
    for (uint32_t k = 0; k < N; k++) {
        cxs[k].me = k;
        mbd_io io = {&cxs[k], send, send, on_stored, on_acked, on_gone, alive, leading, watching, on_mem, on_write, now, on_log};
        node[k] = mbd_new(k, mb_open(NULL, k == 2 ? 64 : (uint64_t)1 << 30, NULL), io);
    }
    /* An own payload on 0: kept (in memory: written at once), counted in memory, pushed to 1. */
    uint8_t pay[48];
    memset(pay, 0x5a, sizeof pay);
    mj_hash h = sha(pay, sizeof pay);
    uint8_t *c = mxalloc(sizeof pay);
    memcpy(c, pay, sizeof pay);
    mbd_local(node[0], 1, h, c, sizeof pay);
    CHECK(stored[0] == 1 && mem[0] == 48 && mb_has_ref(mbd_store(node[0]), 1, &h));
    mbd_push(node[0], 1, 1, h);
    mbd_push(node[0], 1, 1, h); /* queued once */
    mbd_tick(node[0]);
    CHECK_U(nq, 1);
    pump(); /* 1 keeps it and acknowledges */
    CHECK(mb_has_ref(mbd_store(node[1]), 1, &h) && stored[1] == 1);
    pump(); /* the ack reaches 0 */
    CHECK_I(acked_ok[0], 1);
    mbd_done(node[0], 1, h);
    CHECK_I(mem[0], 0);
    /* 2's store is tiny (64 bytes): a push of a second blob is refused (acknowledged not ok). */
    uint8_t big[100];
    memset(big, 1, sizeof big);
    mj_hash hb = sha(big, sizeof big);
    c = mxalloc(sizeof big);
    memcpy(c, big, sizeof big);
    mbd_local(node[0], 1, hb, c, sizeof big);
    mbd_push(node[0], 2, 1, hb);
    mbd_tick(node[0]);
    pump();
    pump();
    CHECK(acked_bad[0] == 1 && !mb_has_ref(mbd_store(node[2]), 1, &hb));
    /* A fetch on 2 for job 1's h: asked of the holder 1, answered, kept. */
    mbd_fetch(node[2], 1, h, 2);
    mbd_tick(node[2]);
    CHECK(nq == 1 && q[0].to == 1 && q[0].b[0] == MESH_F_BLOB_GET);
    pump(); /* 1 answers */
    pump(); /* 2 keeps it */
    CHECK(mb_has_ref(mbd_store(node[2]), 1, &h) && stored[2] == 1);
    /* A corrupt push is dropped. */
    msb f = {0};
    mbf_put(&f, &h, 5, "not the bytes", 13);
    int before = errors;
    mbd_frame(node[1], 0, MESH_F_BLOB_PUT, (const uint8_t *)f.p + 5, f.n - 5);
    msb_free(&f);
    CHECK(errors == before + 1 && !mb_known_ref(mbd_store(node[1]), 5, &h));
    /* Job 1 forgotten on 1: its references go, jobd hears. */
    mbd_news n = {0};
    n.forgotten = mxalloc(sizeof *n.forgotten);
    n.forgotten[0] = 1;
    n.nforgotten = 1;
    mbd_table(node[1], &n);
    CHECK(gone[1] == 1 && !mb_has(mbd_store(node[1]), &h));
    /* A forgotten job's push is refused. */
    mbd_push(node[0], 1, 1, h);
    mbd_tick(node[0]);
    pump();
    pump();
    CHECK_I(acked_bad[0], 2);
    /* On disk: a node whose store has a directory writes through the writers, then reports. */
    char *dir = mfmt("%s/meshd-blobd-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
    mb_remove_tree(dir);
    cx dcx = {0};
    mbd_io io = {&dcx, send, send, on_stored, on_acked, on_gone, alive, leading, watching, on_mem, on_write, now, on_log};
    stored[0] = 0;
    mbd *d = mbd_new(0, mb_open(dir, (uint64_t)1 << 30, NULL), io);
    c = mxalloc(sizeof pay);
    memcpy(c, pay, sizeof pay);
    mbd_local(d, 9, h, c, sizeof pay);
    CHECK(npending == 1 && stored[0] == 0 && mb_known_ref(mbd_store(d), 9, &h) && !mb_has_ref(mbd_store(d), 9, &h));
    mbd *keep = node[0];
    node[0] = d;
    writers(0);
    node[0] = keep;
    CHECK(stored[0] == 1 && mb_has_ref(mbd_store(d), 9, &h));
    size_t got_n;
    uint8_t *got = mb_get(mbd_store(d), &h, &got_n);
    CHECK(got && got_n == sizeof pay && memcmp(got, pay, sizeof pay) == 0);
    free(got);
    mbd_free(d);
    mb_remove_tree(dir);
    free(dir);
    /* render: a one-pass, one-tile scene's job on 0 (the leader assembles active images): its only item's
     * payload kept here, the result committed through news, the image complete. */
    mj_setup setup[] = {{5, 0}, {7, pt_set_arg(PT_H_WIDTH, 32)}, {7, pt_set_arg(PT_H_HEIGHT, 24)}, {7, pt_set_arg(PT_H_PASSES, 1)}, {7, pt_set_arg(PT_H_SPP, 1)}};
    uint32_t sc[PT_DEFAULT_SCENE_WORDS];
    CHECK(masm_job_scene(setup, 5, sc, NULL));
    pt_v *part = mxalloc(PT_TILE_PIX * sizeof *part);
    uint64_t digest = pt_tile_pass_z(sc, 0, 0, part);
    uint8_t *pb = mxalloc(PT_PAYLOAD);
    for (size_t i = 0; i < PT_TILE_PIX; i++) {
        memcpy(pb + 12 * i, &part[i].x, 4);
        memcpy(pb + 12 * i + 4, &part[i].y, 4);
        memcpy(pb + 12 * i + 8, &part[i].z, 4);
    }
    free(part);
    mj_hash ph = sha(pb, PT_PAYLOAD);
    mj_job_id job = 6;
    mbd_local(node[0], job, ph, pb, PT_PAYLOAD);
    mbd_news nw = {0};
    nw.jobs = mxalloc(sizeof *nw.jobs);
    nw.jobs[0] = (mbd_job_info){job, mstrdup("pathtrace"), PT_OP_PASS, mj_setup_clone(setup, 5), 5, 0, true, mstrdup("running")};
    nw.njobs = 1;
    nw.results = mxalloc(sizeof *nw.results);
    nw.results[0] = (mbd_result){job, 0, {ph, PT_PAYLOAD, 1, 0, digest}};
    nw.nresults = 1;
    mbd_table(node[0], &nw); /* the leader starts assembling it, then gets its result */
    CHECK(watched[0][job & 7]);
    nw = (mbd_news){0};
    nw.jobs = mxalloc(sizeof *nw.jobs);
    nw.jobs[0] = (mbd_job_info){job, mstrdup("pathtrace"), PT_OP_PASS, mj_setup_clone(setup, 5), 5, 0, true, mstrdup("running")};
    nw.njobs = 1;
    nw.results = mxalloc(sizeof *nw.results);
    nw.results[0] = (mbd_result){job, 0, {ph, PT_PAYLOAD, 1, 0, digest}};
    nw.nresults = 1;
    mbd_table(node[0], &nw);
    mbd_tick(node[0]); /* read here and folded */
    const char *w[] = {"0.6", "status"};
    uint8_t *out;
    size_t len;
    char *err = NULL;
    CHECK(mbd_render(node[0], w, 2, &out, &len, &err));
    CHECK(strstr((char *)out, "render job 0.6 program pathtrace state running system 0 width 32 height 24 tiles 1 passes 1 spp 1 items 1 committed 1 folded 1 tiles_done 1 tone aces") != NULL);
    CHECK(strstr((char *)out, "complete 1\n") && strstr((char *)out, "\nmember 0 items 1 tiles 1\n"));
    free(out);
    const char *w2[] = {"latest", "ppm"};
    CHECK(mbd_render(node[0], w2, 2, &out, &len, &err) && len == 13 + 32 * 24 * 3 && memcmp(out, "P6\n32 24\n255\n", 13) == 0);
    free(out);
    const char *w3[] = {"0.7"};
    CHECK(!mbd_render(node[0], w3, 1, &out, &len, &err));
    CHECK_S(err, "no job 0.7 (committed here)");
    free(err);
    for (uint32_t k = 0; k < N; k++) mbd_free(node[k]);
    return mesh_check_done();
}
