/* The jobs thread and the blob thread over a mesh_shared in-process, with a fake driver: a single meshd
 * runs a job with payloads to the end (leases, results, payload blobs made durable, the commit, the
 * host block, the jobs view), the control answers and their errors, `render`, frame routing, backpressure
 * on payload memory, a restart replaying the job log and indexing the blob store, and a follower in a
 * fleet forwarding changes to the fleet thread and answering a remote request with F_CTL_REPLY. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags */
#include "mesh/jobd/jobd.h"

#include "mesh/abi/host_jobs.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/blobd/blobd.h"
#include "mesh/blobs/durable.h"
#include "mesh/blobs/store.h"
#include "mesh/check.h"
#include "mesh/strbuf.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static _Atomic bool drv_stop;
static _Atomic uint32_t leases, items_run;

/* The driver: runs job program "p"; each item's result is 2 * item, its out the item (8 bytes), its
 * payload 64 bytes made from the item. */
static void *driver(void *arg) {
    mesh_shared *sh = arg;
    while (!atomic_load(&drv_stop)) {
        mesh_cmd c;
        if (mesh_chan_recv(sh->cmd, &c, 20) != MESH_CHAN_OK) continue;
        if (c.k == MESH_CMD_JOB_PROGS) {
            mesh_job_progs *p = mxalloc(sizeof *p);
            p->v = mxalloc(sizeof *p->v);
            p->v[0] = (mj_prog){mstrdup("p"), 0, 16};
            p->n = 1;
            mesh_reply_ptr(c.reply, p, mesh_job_progs_free);
            c.reply = NULL;
        } else if (c.k == MESH_CMD_JOB_LEASE) {
            atomic_fetch_add(&leases, 1);
            for (size_t r = 0; r < c.lease.items.n; r++)
                for (uint64_t it = c.lease.items.v[r][0]; it < c.lease.items.v[r][1]; it++) {
                    mesh_job_event e = mesh_job_event_new(MESH_JEV_DONE);
                    e.res.lease = c.lease.lease;
                    e.res.job = c.lease.job;
                    e.res.item = it;
                    e.res.status = MESH_JOBS_RS_OK;
                    e.res.result = (uint32_t)(2 * it);
                    e.res.out = mxalloc(8);
                    for (int b = 0; b < 8; b++) e.res.out[b] = (uint8_t)(it >> (8 * b));
                    e.res.out_len = 8;
                    e.has_payload = true;
                    e.payload_len = 64;
                    e.payload = mxalloc(64);
                    for (int b = 0; b < 64; b++) e.payload[b] = (uint8_t)(it * 31 + (uint64_t)b);
                    atomic_fetch_add(&items_run, 1);
                    if (mesh_chan_send(sh->jobs, &e) != MESH_CHAN_OK) mesh_job_event_free(&e);
                }
        }
        mesh_cmd_free(&c);
    }
    return NULL;
}

/* A control line through the jobs thread, as control.c sends it. */
static bool ctl(mesh_shared *sh, const char *line, char **text) {
    mesh_reply *r = mesh_reply_new();
    mesh_job_event e = mesh_job_event_new(MESH_JEV_CTL);
    e.line = mstrdup(line);
    e.reply_to = MESH_JOB_REPLY_LOCAL;
    e.reply = mesh_reply_sender(r);
    CHECK(mesh_chan_send(sh->jobs, &e) == MESH_CHAN_OK);
    mesh_result x;
    int w = mesh_reply_wait(r, 20000, &x);
    mesh_reply_release(r);
    CHECK_I(w, MESH_CHAN_OK);
    if (w != MESH_CHAN_OK) {
        *text = mstrdup("");
        return false;
    }
    bool ok = x.ok;
    *text = x.text ? x.text : mstrdup("");
    x.text = NULL;
    mesh_result_free(&x);
    return ok;
}

static bool wait_for(mesh_shared *sh, size_t off, uint64_t want, int ms) {
    for (int i = 0; i < ms / 10; i++) {
        uint64_t v = off == MESH_S_JOB_DONE ? (uint64_t)mesh_host_block_get_u32(&sh->host, off) | (uint64_t)mesh_host_block_get_u32(&sh->host, off + 4) << 32
                                            : mesh_host_block_get_u32(&sh->host, off);
        if (v == want) return true;
        mesh_sleep_us(10000);
    }
    return false;
}

/* A field of the first record of kind (and job, when given) in the jobs view (a copy, or NULL). */
static char *view_field(mesh_shared *sh, const char *kind, const char *job, const char *key) {
    char *v = NULL;
    pthread_mutex_lock(&sh->mu);
    for (size_t i = 0; i < sh->jobs_view_recs.len && !v; i++) {
        const mesh_rec *r = &sh->jobs_view_recs.items[i];
        if (strcmp(r->kind, kind)) continue;
        const char *j = mesh_rec_get(r, "job");
        if (job && (!j || strcmp(j, job))) continue;
        const char *x = mesh_rec_get(r, key);
        v = mstrdup(x ? x : "-");
    }
    pthread_mutex_unlock(&sh->mu);
    return v;
}

static size_t view_count(mesh_shared *sh, const char *kind) {
    size_t n = 0;
    pthread_mutex_lock(&sh->mu);
    for (size_t i = 0; i < sh->jobs_view_recs.len; i++) n += strcmp(sh->jobs_view_recs.items[i].kind, kind) == 0;
    pthread_mutex_unlock(&sh->mu);
    return n;
}

static void single(const char *dir) {
    static _Atomic bool quit;
    char *jobs = mfmt("%s/jobs.log", dir), *blobs = mfmt("%s/blobs", dir);
    mjd_config cfg = {jobs, blobs, 64u << 20, MESH_PEER_DEAD_MS};
    for (int run = 0; run < 2; run++) {
        mesh_mailbox mb = mesh_mailbox_test();
        mesh_shared *sh = mesh_shared_new(mb, "x", false, 0, 1, &quit);
        char *err = NULL;
        mjd *d = mjd_open(sh, &cfg, &err);
        CHECK(d != NULL);
        if (!d) {
            fprintf(stderr, "%s\n", err);
            exit(1);
        }
        CHECK(sh->blobs != NULL && sh->jobs_ext != NULL);
        atomic_store(&drv_stop, false);
        pthread_t th;
        pthread_create(&th, NULL, driver, sh);
        mjd_start(d);
        char *t;
        if (run == 0) {
            CHECK(ctl(sh, "job submit p items 0-20", &t));
            CHECK(strncmp(t, "job 1.1 program p items 20 on 1 member(s) (0)", 45) == 0);
            free(t);
            CHECK(wait_for(sh, MESH_S_JOB_DONE, 20, 20000));
            CHECK(wait_for(sh, MESH_S_JOBS_ACTIVE, 0, 20000));
            CHECK_U(atomic_load(&items_run), 20);
            CHECK(atomic_load(&leases) >= 1);
            mesh_sleep_us(400000); /* a publish after the last commit */
            CHECK_U(mesh_host_block_get_u32(&sh->host, MESH_S_JOBS_TOTAL), 1);
            CHECK_U(mesh_host_block_get_u32(&sh->host, MESH_S_JOB_FLAGS), MESH_JF_LEADER | MESH_JF_ACTIVE | MESH_JF_PERSIST);
            char *v = view_field(sh, "jobs", NULL, "role");
            CHECK_S(v, "leader");
            free(v);
            v = view_field(sh, "job", "1.1", "state");
            CHECK_S(v, "done");
            free(v);
            v = view_field(sh, "job", "1.1", "done");
            CHECK_S(v, "20");
            free(v);
            v = view_field(sh, "job", "1.1", "progress");
            CHECK_S(v, "1.0000");
            free(v);
            v = view_field(sh, "share", "1.1", "done");
            CHECK_S(v, "20");
            free(v);
            CHECK_U(view_count(sh, "result"), 3);
            v = view_field(sh, "result", "1.1", "item");
            CHECK_S(v, "19");
            free(v);
            v = view_field(sh, "result", "1.1", "out");
            CHECK_S(v, "1300000000000000");
            free(v);

            /* The control answers and their errors. */
            CHECK(ctl(sh, "job status 1.1", &t));
            CHECK(strstr(t, "1.1") != NULL && strstr(t, "done") != NULL);
            free(t);
            CHECK(ctl(sh, "job results 1.1 18", &t));
            const char *head = "results 1.1 total 20 from 18 count 2 done 20 failed 0 dupes 0 state done\n18 36 lease ";
            CHECK(strncmp(t, head, strlen(head)) == 0);
            CHECK(strstr(t, "\n19 38 lease ") != NULL && strstr(t, "\n17 ") == NULL);
            CHECK(strstr(t, " out 1200000000000000 blob ") != NULL);
            free(t);
            CHECK(!ctl(sh, "job status x", &t));
            CHECK_S(t, "\"x\": a job id is <term>.<n>");
            free(t);
            CHECK(!ctl(sh, "job results 1.1 zz", &t));
            CHECK_S(t, "\"zz\": a number");
            free(t);
            CHECK(!ctl(sh, "frob it", &t));
            CHECK_S(t, "not a job request: \"frob it\"");
            free(t);
            CHECK(!ctl(sh, "job status 1.9", &t));
            CHECK(strstr(t, "no job 1.9") != NULL);
            free(t);
            CHECK(ctl(sh, "jobs", &t));
            CHECK(strstr(t, "1.1") != NULL);
            free(t);

            /* render: not a pathtrace job; none at all. */
            uint8_t *b;
            size_t n;
            char *w1[] = {"1.1"}, *w0[] = {"latest"};
            CHECK(!mjd_render(sh, w1, 1, &b, &n, &err));
            CHECK(strstr(err, "is not a pathtrace image") != NULL);
            free(err);
            CHECK(!mjd_render(sh, w0, 1, &b, &n, &err));
            CHECK_S(err, "no pathtrace job in the committed table");
            free(err);

            /* Frame routing: neither a job nor a blob frame; a length that does not match; a short blob frame (logged). */
            uint8_t f[9] = {MESH_F_CTL, 4, 0, 0, 0, 1, 2, 3, 4};
            CHECK(!mjd_route_frame(sh, 1, f, 9));
            f[0] = MESH_F_BLOB_GET;
            CHECK(!mjd_route_frame(sh, 1, f, 8));
            CHECK(mjd_route_frame(sh, 1, f, 9));

            /* Backpressure: while blobd holds too many payloads in memory, this member takes no leases (it
             * reports no job program: a submit finds no member that runs it). */
            uint32_t before = atomic_load(&leases);
            atomic_store(&sh->blob_mem, MESH_BLOBD_BLOB_MEM_MAX + 1);
            mesh_sleep_us(600000);
            CHECK(!ctl(sh, "job submit p items 0-5", &t));
            CHECK(strncmp(t, "no member runs p", 16) == 0);
            free(t);
            CHECK_U(atomic_load(&leases), before);
            atomic_store(&sh->blob_mem, 0);
            mesh_sleep_us(600000);
            CHECK(ctl(sh, "job submit p items 0-5", &t));
            free(t);
            CHECK(wait_for(sh, MESH_S_JOB_DONE, 25, 20000));
            CHECK(atomic_load(&leases) > before);
            CHECK(ctl(sh, "job forget 1.2", &t));
            free(t);
        } else {
            /* The restart: the log replayed (1.2 forgotten), the blobs found. */
            CHECK(wait_for(sh, MESH_S_JOBS_TOTAL, 1, 20000));
            CHECK(wait_for(sh, MESH_S_JOB_DONE, 20, 20000));
            CHECK(ctl(sh, "job status 1.1", &t));
            CHECK(strstr(t, "done") != NULL);
            free(t);
            CHECK(!ctl(sh, "job status 1.2", &t));
            free(t);
        }
        mjd_stop(d);
        atomic_store(&drv_stop, true);
        pthread_join(th, NULL);
        mjd_free(d);
        CHECK(sh->blobs == NULL && sh->jobs_ext == NULL);
        mesh_shared_free(sh);
        free(mb.base);
    }
    /* Every payload of job 1.1 is durable in the store (1.2's went with its forget). */
    char *err = NULL;
    mb_store *s = mb_open(blobs, 64u << 20, &err);
    CHECK(s != NULL);
    if (s) {
        CHECK_U(mb_count(s), 20);
        mb_store_free(s);
    }
    free(jobs);
    free(blobs);
}

/* A follower of a fleet of 3 (system 1, member 0 leads): reads are answered here, changes go to the
 * fleet thread, a remote request is answered with F_CTL_REPLY on the link. */
static void follower(void) {
    static _Atomic bool quit;
    mesh_mailbox mb = mesh_mailbox_test();
    mesh_shared *sh = mesh_shared_new(mb, "x", true, 1, 3, &quit);
    mjd_config cfg = {NULL, NULL, 16u << 20, MESH_PEER_DEAD_MS};
    char *err = NULL;
    mjd *d = mjd_open(sh, &cfg, &err);
    CHECK(d != NULL);
    pthread_mutex_lock(&sh->mu);
    sh->link_out[0] = mesh_link_queue_new(4096);
    pthread_mutex_unlock(&sh->mu);
    mjd_set_view(sh, (mj_view){5, true, 0, 7, 7});
    atomic_store(&drv_stop, false);
    pthread_t th;
    pthread_create(&th, NULL, driver, sh);
    mjd_start(d);
    char *t;
    CHECK(ctl(sh, "jobs", &t));
    free(t);
    /* A change: forwarded to the fleet thread, whose answer reaches the caller. */
    mesh_reply *r = mesh_reply_new();
    mesh_job_event e = mesh_job_event_new(MESH_JEV_CTL);
    e.line = mstrdup("job cancel 1.1");
    e.reply_to = MESH_JOB_REPLY_LOCAL;
    e.reply = mesh_reply_sender(r);
    CHECK(mesh_chan_send(sh->jobs, &e) == MESH_CHAN_OK);
    mesh_peer_event pe;
    CHECK_I(mesh_chan_recv(sh->peer, &pe, 20000), MESH_CHAN_OK);
    CHECK(pe.k == MESH_PEV_CHANGE);
    CHECK_S(pe.line, "job cancel 1.1");
    mesh_reply_ok(pe.reply, "forwarded");
    pe.reply = NULL;
    mesh_peer_event_free(&pe);
    mesh_result x;
    CHECK_I(mesh_reply_wait(r, 20000, &x), MESH_CHAN_OK);
    CHECK(x.ok);
    CHECK_S(x.text, "forwarded");
    mesh_result_free(&x);
    mesh_reply_release(r);
    /* A remote request (F_CTL from member 0, id 7): its answer on the link. */
    e = mesh_job_event_new(MESH_JEV_CTL);
    e.line = mstrdup("job status zz");
    e.reply_to = MESH_JOB_REPLY_REMOTE;
    e.remote_from = 0;
    e.remote_id = 7;
    CHECK(mesh_chan_send(sh->jobs, &e) == MESH_CHAN_OK);
    bool seen = false;
    for (int i = 0; i < 2000 && !seen; i++) {
        mesh_link_frame f;
        if (mesh_chan_recv(sh->link_out[0], &f, 10) != MESH_CHAN_OK) continue;
        if (f.n >= 13 && f.b[0] == MESH_F_CTL_REPLY) {
            seen = true;
            const char *want = "\"zz\": a job id is <term>.<n>";
            CHECK_U(f.b[1], 7);
            CHECK_U(f.b[5], 0);
            CHECK_U(f.b[9], strlen(want));
            CHECK(f.n == 13 + strlen(want) && memcmp(f.b + 13, want, strlen(want)) == 0);
        }
        free(f.b);
    }
    CHECK(seen);
    mesh_sleep_us(400000);
    char *v = view_field(sh, "jobs", NULL, "role");
    CHECK_S(v, "follower");
    free(v);
    v = view_field(sh, "jobs", NULL, "leader");
    CHECK_S(v, "0");
    free(v);
    mjd_stop(d);
    atomic_store(&drv_stop, true);
    pthread_join(th, NULL);
    mjd_free(d);
    mesh_shared_free(sh);
    free(mb.base);
}

int main(void) {
    char tmpl[] = "/tmp/mesh-jobd-XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    single(dir);
    follower();
    mb_remove_tree(dir);
    return mesh_check_done();
}
