/* The jobs thread's state (jobd.rs `Jobd`). */
#ifndef MESH_JOBD_JOBS_THREAD_H
#define MESH_JOBD_JOBS_THREAD_H
#include "internal.h"
#include "mesh/omap.h"

typedef struct {
    mjd *d;
    mesh_shared *sh;
    mj_node *node;
    mj_disk disk;
    int64_t t0;
    bool peered;
    momap *replies; /* token -> jd_reply */
    uint64_t token;
    int64_t progs_at;
    mesh_reply *progs; /* the driver's JOB_PROGS answer awaited */
    int64_t publish_at;
    /* Per job: its done count over the last few seconds (the view's items/s and ETA). */
    momap *hist; /* job -> jd_hist */
    /* Per watched image: the items whose committed results blobd has. */
    momap *sent; /* job -> mj_ranges */
    /* Forgotten jobs blobd was told of (each once a process, so a job forgotten while this member was
     * down -- replayed from the log -- is told at the start and its blobs here go too). */
    momap *told; /* job -> (nothing) */
    /* Leases refused while blobd holds too many of our payloads in memory (logged on a change). */
    bool throttled;
} jd;

typedef struct {
    int64_t *at; /* mono ns */
    uint64_t *done;
    size_t n, cap;
} jd_hist;

uint64_t jd_unix_ms(void);
mj_view jd_view(const jd *x);
/* The host block, the jobs view's records, blobd's news (every 250 ms). */
void jd_publish(jd *x);
/* The jobs view's records (exposed for the tests through jd_publish's result in sh->jobs_view_recs). */
mesh_recs jd_jobs_recs(jd *x, double rate);

#endif
