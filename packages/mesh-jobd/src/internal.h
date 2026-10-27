/* mesh-jobd's own state: the events on sh->blobs, the area's state in sh->jobs_ext, the threads. */
#ifndef MESH_JOBD_INTERNAL_H
#define MESH_JOBD_INTERNAL_H
#include "mesh/blobd/blobd.h"
#include "mesh/jobd/jobd.h"
#include "mesh/jobd/store.h"
#include "mesh/reply.h"

#include <pthread.h>
#include <stdatomic.h>

/* The blob thread's events (blobd.rs BlobEvent). */
typedef enum {
    MJD_BEV_LOCAL,   /* job, hash, bytes: this member's own payload */
    MJD_BEV_PUSH,    /* to, job, hash */
    MJD_BEV_FETCH,   /* job, hash, holders */
    MJD_BEV_DONE,    /* job, hash */
    MJD_BEV_FRAME,   /* from, ty, bytes: the body after the length */
    MJD_BEV_TABLE,   /* news */
    MJD_BEV_RENDER,  /* words, reply */
    MJD_BEV_WRITTEN, /* job, hash, why, from, ok, err */
} mjd_bev_kind;

typedef struct {
    mjd_bev_kind k;
    uint32_t from, to, holders;
    uint8_t ty;
    mj_job_id job;
    mj_hash hash;
    uint8_t *bytes;
    size_t n;
    mbd_news *news;
    char **words;
    size_t nwords;
    mesh_reply *reply;
    mbd_why why;
    bool ok;
    char *err;
} mjd_bev;

void mjd_bev_free(void *e);
/* Sends on sh->blobs (an event that cannot be sent is freed). */
void mjd_blobs_send(mesh_shared *sh, mjd_bev *e);

/* sh->jobs_ext: what the fleet thread and the blob thread give the jobs thread. */
typedef struct {
    pthread_mutex_t mu;
    mj_view view;
    mj_sched *sched;    /* the scheduler's model not picked up yet */
    mj_job_id *watch;   /* the images assembled here (blobd: render_watch) */
    size_t nwatch, capwatch;
} mjd_ext;

static inline mjd_ext *mjd_ext_of(mesh_shared *sh) { return sh->jobs_ext; }
mj_view mjd_ext_view(mesh_shared *sh);
/* A copy of the watch set (free it). */
size_t mjd_ext_watch(mesh_shared *sh, mj_job_id **out);
void mjd_ext_set_watch(mesh_shared *sh, mj_job_id id, bool on);

struct mjd {
    mesh_shared *sh;
    mjd_ext *ext;
    mj_cfg cfg;
    mjd_store *store;
    mj_replica rep; /* the jobs thread's node takes it */
    mb_store *bstore; /* the blob thread's mbd takes it */
    mesh_chan *wq;    /* mbd_write, to the writers */
    pthread_t jobs_t, blobs_t, writers[MESH_BLOBD_WRITERS];
    _Atomic bool stop;
    bool started;
};

void *mjd_jobs_main(void *d);
void *mjd_blobs_main(void *d);
void *mjd_writer_main(void *d);

#endif
