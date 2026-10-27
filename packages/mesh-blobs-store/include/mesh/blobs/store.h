/* @ajax/mesh-blobs-store: a member's blob store (blobs.rs `Store`): job items' payloads, off the job
 * log, content-addressed by their sha256 and referenced per job. A job's blob is the file
 * `<dir>/<job>/<sha256 hex>` (@ajax/mesh-blobs-durable writes it), so it is either whole under its
 * name or absent. A read checks the bytes against the name: a corrupt file is removed and reads as
 * absent.
 *
 * One blob, several jobs: two jobs that render the same scene compute byte-identical payloads. Each
 * job holds its own reference (a hard link of the file another job's reference names, so the bytes
 * are on disk once). Forgetting or evicting one job removes only its references; a blob goes when no
 * job references it.
 *
 * Bounded: at most `cap` bytes (each blob counted once). Room is made only by evicting forgotten jobs'
 * references; a store full of listed jobs' blobs refuses. Without a directory the blobs are kept in
 * memory, under the same cap. */
#ifndef MESH_BLOBS_STORE_H
#define MESH_BLOBS_STORE_H

#include "mesh/jobs/id.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A job's standing for eviction (from the committed job table). */
typedef enum {
    MB_LISTED,    /* in the committed table (active or finished), or not known here yet: kept */
    MB_FORGOTTEN, /* forgotten (`job forget`): its references may go */
} mb_live;

typedef mb_live (*mb_live_fn)(void *ctx, mj_job_id job);

typedef struct {
    mj_job_id job;
    mj_hash h;
} mb_ref;

typedef struct {
    mb_ref *v;
    size_t n;
} mb_refs;

void mb_refs_free(mb_refs *r);

/* How a reserved reference is made durable: write the bytes (link false), or hard-link `path`
 * (another job's whole file of the same bytes; "" in a store without a directory) under the new name. */
typedef struct {
    bool link;
    char *path;
    mb_refs evicted; /* the references evicted for room */
} mb_reserved;

void mb_reserved_free(mb_reserved *r);

typedef struct mb_store mb_store;

/* Opens (creates) the store: every whole blob under dir indexed (a blob named in several job
 * directories is one blob with several references), leftover .tmps removed. dir NULL: in memory.
 * log (may be NULL) gets the store's error lines. */
mb_store *mb_open(const char *dir, uint64_t cap, char **err);
void mb_store_free(mb_store *s);
void mb_set_log(mb_store *s, void (*log)(void *ctx, const char *line), void *ctx);
uint64_t mb_cap(const mb_store *s);

bool mb_persisted(const mb_store *s);
uint64_t mb_bytes(const mb_store *s);  /* each blob once */
size_t mb_count(const mb_store *s);    /* blobs */
uint64_t mb_refs_total(const mb_store *s); /* references (a blob of two jobs counts twice) */
/* Whole and named under some job (its bytes can be read). */
bool mb_has(const mb_store *s, const mj_hash *h);
/* Job's reference is whole and named: this member's copy for that job. */
bool mb_has_ref(const mb_store *s, mj_job_id job, const mj_hash *h);
/* Job's reference is present or being made. */
bool mb_known_ref(const mb_store *s, mj_job_id job, const mj_hash *h);
/* Every reference present (job, hash). */
mb_refs mb_all(const mb_store *s);
/* A job's directory (NULL without one; the caller frees it). */
char *mb_dir_of(const mb_store *s, mj_job_id job);

/* A reference for job to blob h of len bytes: pending until mb_written. A blob another job references
 * already costs no room (a link when its file is whole); a new one may evict forgotten jobs'
 * references. False with *err when the store is full of listed jobs' blobs. The reference must not
 * exist yet. */
bool mb_reserve(mb_store *s, mj_job_id job, mj_hash h, uint32_t len, mb_live_fn live, void *live_ctx, mb_reserved *out, char **err);
/* A reserved reference was made (ok) or failed. A reference dropped meanwhile (its job forgotten
 * while the writer ran): the file it left goes. */
void mb_written(mb_store *s, mj_job_id job, const mj_hash *h, bool ok);
/* Without a directory: the reserved reference's bytes kept in memory (present at once). */
void mb_put_mem(mb_store *s, mj_job_id job, mj_hash h, const void *bytes, size_t n);
/* A present blob's bytes (any job's reference), checked against its name; a file that doesn't match
 * is removed, and every reference that named it reads as absent (mb_take_lost). NULL if none. */
uint8_t *mb_get(mb_store *s, const mj_hash *h, size_t *n);
/* References dropped by mb_get since the last call. */
mb_refs mb_take_lost(mb_store *s);
/* Every reference of a job removed (its directory with them): their hashes. */
mb_refs mb_drop_job(mb_store *s, mj_job_id job);
/* Jobs with references here, in id order. */
size_t mb_jobs(const mb_store *s, mj_job_id **out);

#endif
