/* @ajax/mesh-blobd: the blob thread's logic (blobd.rs), sans threads: this member's blob store
 * (@ajax/mesh-blobs-store), the blob frames on the peer links, and the image assemblies.
 *
 * - Its own payloads (jobd, MBD_LOCAL): reserved in the store, written by the writers (.tmp, fsync,
 *   rename; a directory fsync per batch), kept in memory until their copies are durable (MBD_DONE);
 *   their bytes in memory are counted (blob_mem: jobd stops taking leases past MBD_BLOB_MEM_MAX).
 * - Pushes (jobs MJ_OUT_PUSH_BLOB): F_BLOB_PUT through the link's bulk queue, at most PUT_WINDOW
 *   unacknowledged per member; F_BLOB_ACK once the receiver's copy is durable (or refused: full).
 * - Every copy is a job's: a PUT, an own payload or a kept fetch makes that job's reference, even when
 *   the same bytes are here for another job.
 * - Fetches (the leader's parked results; an assembly's next passes): F_BLOB_GET to a holder (else any
 *   member up), one at a time per blob, the next holder on a refusal, a timeout or a blob that does not
 *   hash to its name; at most FETCH_MAX in flight.
 * - Assemblies: the jobs a viewer asked for (`render`) and, on the leader, every active one: jobd sends
 *   the committed results of the watched jobs (MBD_TABLE); each tile's passes are read (local) or
 *   fetched and folded in pass order (@ajax/mesh-assemble).
 *
 * The world is an mbd_io: the peer links, jobd's channel, the writers, the clock. */
#ifndef MESH_BLOBD_H
#define MESH_BLOBD_H

#include "mesh/blobs/store.h"
#include "mesh/jobs/id.h"
#include "mesh/jobs/spec.h"
#include "mesh/assemble.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESH_BLOBD_PUT_WINDOW 128u       /* unacknowledged pushes per member */
#define MESH_BLOBD_PUT_TIMEOUT_MS 5000u  /* a push not acknowledged in this long is forgotten here */
#define MESH_BLOBD_FETCH_MAX 256u        /* blob fetches in flight, at most */
#define MESH_BLOBD_FETCH_TIMEOUT_MS 2000u
#define MESH_BLOBD_LOCAL_READS 400u      /* an assembly's local reads per tick, at most */
#define MESH_BLOBD_BLOB_MEM_MAX ((uint64_t)256 << 20)
#define MESH_BLOBD_WRITERS 3u

/* Why a blob is being written. */
typedef enum { MBD_WHY_LOCAL, MBD_WHY_PUT, MBD_WHY_FETCHED } mbd_why;

/* A reference to make durable (for the writers). */
typedef struct {
    char *dir;   /* its job's directory */
    mj_hash hash;
    char *link;  /* a file of the same bytes to hard-link (another job's), or NULL */
    uint8_t *bytes;
    size_t n;
    mbd_why why;
    uint32_t from; /* MBD_WHY_PUT: the pusher */
    mj_job_id job;
} mbd_write;

void mbd_write_free(mbd_write *w);

/* What the jobs thread knows of a job (every 250 ms, from the committed table). */
typedef struct {
    mj_job_id id;
    char *program;
    uint32_t op;
    mj_setup *setup;
    size_t nsetup;
    uint32_t tone;
    bool active;
    char *state;
} mbd_job_info;

/* A watched image's new committed payload result. */
typedef struct {
    mj_job_id job;
    uint64_t item;
    masm_item it;
} mbd_result;

/* The committed table's news for this thread. */
typedef struct {
    mbd_job_info *jobs;
    size_t njobs;
    mj_job_id *forgotten;
    size_t nforgotten;
    mbd_result *results;
    size_t nresults;
} mbd_news;

void mbd_news_free(mbd_news *n);

/* The world as blobd sees it. Frames are whole (type, u32 length, body). */
typedef struct {
    void *ctx;
    bool (*send_peer)(void *ctx, uint32_t to, const uint8_t *frame, size_t n);
    bool (*send_bulk)(void *ctx, uint32_t to, const uint8_t *frame, size_t n);
    /* To jobd: job's copies of these hashes are durable here; member `from` acknowledged (or refused) a
     * copy; these references are gone. */
    void (*stored)(void *ctx, const mb_ref *refs, size_t n);
    void (*acked)(void *ctx, uint32_t from, mj_job_id job, const mj_hash *h, bool ok);
    void (*gone)(void *ctx, const mb_ref *refs, size_t n);
    uint32_t (*alive)(void *ctx);   /* members alive (this one included) */
    bool (*leading)(void *ctx);     /* this member leads the job table (or a single meshd) */
    void (*watching)(void *ctx, mj_job_id job, bool on); /* the images assembled here (jobd sends their results) */
    void (*mem)(void *ctx, int64_t delta);               /* bytes of own payloads in memory */
    void (*write)(void *ctx, mbd_write w);               /* to the writers (takes w); they answer mbd_written */
    int64_t (*now_ms)(void *ctx);
    void (*log)(void *ctx, bool error, const char *line);
} mbd_io;

typedef struct mbd mbd;

/* Takes store. At start the jobs thread learns what is here (io.stored). */
mbd *mbd_new(uint32_t me, mb_store *store, mbd_io io);
void mbd_free(mbd *b);
mb_store *mbd_store(mbd *b);

/* The events (blobd.rs BlobEvent). */
void mbd_local(mbd *b, mj_job_id job, mj_hash hash, uint8_t *bytes, size_t n); /* takes bytes */
void mbd_push(mbd *b, uint32_t to, mj_job_id job, mj_hash hash);
void mbd_fetch(mbd *b, mj_job_id job, mj_hash hash, uint32_t holders);
void mbd_done(mbd *b, mj_job_id job, mj_hash hash);
/* A blob frame from a peer link: its type and the body after the length. */
void mbd_frame(mbd *b, uint32_t from, uint8_t ty, const uint8_t *body, size_t n);
void mbd_table(mbd *b, mbd_news *news); /* takes news */
/* A writer finished job's reference to hash (ok, or why not). */
void mbd_written(mbd *b, mj_job_id job, mj_hash hash, mbd_why why, uint32_t from, bool ok, const char *err);
/* `render [<job>|latest] [status|ppm|bmp|raw]`: the answer's bytes, or false with *err. */
bool mbd_render(mbd *b, const char *const *words, size_t n, uint8_t **out, size_t *len, char **err);
/* The periodic work: pushes, the assemblies' reads and fetches, fetch timeouts, stale assemblies. */
void mbd_tick(mbd *b);
/* References whose file turned out missing or corrupt since the last call go to jobd (io.gone). */
void mbd_report_lost(mbd *b);

/* The writers' work on one batch: each reference made durable (mb_make_durable), each directory fsynced
 * once; ok[i] and err[i] (free it) per write. */
void mbd_write_batch(mbd_write *ws, size_t n, bool *ok, char **err);

#endif
